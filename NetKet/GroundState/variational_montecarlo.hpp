// Copyright 2018 The Simons Foundation, Inc. - All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef NETKET_VARIATIONALMONTECARLO_HPP
#define NETKET_VARIATIONALMONTECARLO_HPP

#include <complex>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include <Eigen/Dense>
#include <nonstd/optional.hpp>

#include "Machine/machine.hpp"
#include "Operator/abstract_operator.hpp"
#include "Optimizer/optimizer.hpp"
#include "Optimizer/stochastic_reconfiguration.hpp"
#include "Output/json_output_writer.hpp"
#include "Sampler/abstract_sampler.hpp"
#include "Stats/stats.hpp"
#include "Utils/parallel_utils.hpp"
#include "Utils/random_utils.hpp"
#include "common_types.hpp"

namespace netket {

// Variational Monte Carlo schemes to learn the ground state
// Available methods:
// 1) Stochastic reconfiguration optimizer
//   both direct and sparse version
// 2) Gradient Descent optimizer
class VariationalMonteCarlo {
  using GsType = Complex;
  using VectorT = Eigen::Matrix<Complex, Eigen::Dynamic, 1>;
  using MatrixT = Eigen::Matrix<Complex, Eigen::Dynamic, Eigen::Dynamic>;

  const AbstractOperator &ham_;
  AbstractSampler &sampler_;
  AbstractMachine &psi_;

  std::vector<std::vector<int>> connectors_;
  std::vector<std::vector<double>> newconfs_;
  std::vector<Complex> mel_;

  Eigen::VectorXcd elocs_;
  MatrixT Ok_;
  VectorT Okmean_;

  Eigen::MatrixXd vsamp_;

  Eigen::VectorXcd grad_;

  int totalnodes_;
  int mynode_;

  AbstractOptimizer &opt_;
  SR sr_;
  bool dosr_;

  std::vector<AbstractOperator *> obs_;
  std::vector<std::string> obsnames_;
  ObsManager obsmanager_;

  int nsamples_;
  int nsamples_node_;
  int ninitsamples_;
  int ndiscardedsamples_;

  Complex elocmean_;
  double elocvar_;
  int npar_;

 public:
  class Iterator {
   public:
    // typedefs required for iterators
    using iterator_category = std::input_iterator_tag;
    using difference_type = Index;
    using value_type = Index;
    using pointer_type = Index *;
    using reference_type = Index &;

   private:
    VariationalMonteCarlo &vmc_;
    Index step_size_;
    nonstd::optional<Index> n_iter_;

    Index cur_iter_;

   public:
    Iterator(VariationalMonteCarlo &vmc, Index step_size,
             nonstd::optional<Index> n_iter)
        : vmc_(vmc),
          step_size_(step_size),
          n_iter_(std::move(n_iter)),
          cur_iter_(0) {}

    Index operator*() const { return cur_iter_; }

    Iterator &operator++() {
      vmc_.Advance(step_size_);
      cur_iter_ += step_size_;
      return *this;
    }

    // TODO(C++17): Replace with comparison to special Sentinel type, since
    // C++17 allows end() to return a different type from begin().
    bool operator!=(const Iterator &) {
      return !n_iter_.has_value() || cur_iter_ < n_iter_.value();
    }
    // pybind11::make_iterator requires operator==
    bool operator==(const Iterator &other) { return !(*this != other); }

    Iterator begin() const { return *this; }
    Iterator end() const { return *this; }
  };

  VariationalMonteCarlo(const AbstractOperator &hamiltonian,
                        AbstractSampler &sampler, AbstractOptimizer &optimizer,
                        int nsamples, int discarded_samples = -1,
                        int discarded_samples_on_init = 0,
                        const std::string &method = "Sr",
                        double diag_shift = 0.01, bool use_iterative = false,
                        bool use_cholesky = true)
      : ham_(hamiltonian),
        sampler_(sampler),
        psi_(sampler.GetMachine()),
        opt_(optimizer),
        elocvar_(0.) {
    Init(nsamples, discarded_samples, discarded_samples_on_init, method,
         diag_shift, use_iterative, use_cholesky);
  }

  void Init(int nsamples, int discarded_samples, int discarded_samples_on_init,
            const std::string &method, double diag_shift, bool use_iterative,
            bool use_cholesky) {
    npar_ = psi_.Npar();

    opt_.Init(npar_, psi_.IsHolomorphic());

    grad_.resize(npar_);
    Okmean_.resize(npar_);

    MPI_Comm_size(MPI_COMM_WORLD, &totalnodes_);
    MPI_Comm_rank(MPI_COMM_WORLD, &mynode_);

    nsamples_ = nsamples;

    nsamples_node_ = int(std::ceil(double(nsamples_) / double(totalnodes_)));

    ninitsamples_ = discarded_samples_on_init;

    if (discarded_samples == -1) {
      ndiscardedsamples_ = 0.1 * nsamples_node_;
    } else {
      ndiscardedsamples_ = discarded_samples;
    }

    if (method == "Gd") {
      dosr_ = false;
      InfoMessage() << "Using a gradient-descent based method" << std::endl;
    } else {
      setSrParameters(diag_shift, use_iterative, use_cholesky);
    }

    InfoMessage() << "Variational Monte Carlo running on " << totalnodes_
                  << " processes" << std::endl;

    MPI_Barrier(MPI_COMM_WORLD);
  }

  void AddObservable(AbstractOperator &ob, const std::string &obname) {
    obs_.push_back(&ob);
    obsnames_.push_back(obname);
  }

  void InitSweeps() {
    sampler_.Reset();

    for (int i = 0; i < ninitsamples_; i++) {
      sampler_.Sweep();
    }
  }

  void Sample() {
    sampler_.Reset();

    for (int i = 0; i < ndiscardedsamples_; i++) {
      sampler_.Sweep();
    }

    vsamp_.resize(nsamples_node_, psi_.Nvisible());

    for (int i = 0; i < nsamples_node_; i++) {
      sampler_.Sweep();
      vsamp_.row(i) = sampler_.Visible();
    }
  }

  /**
   * Computes the expectation values of observables from the currently stored
   * samples.
   */
  void ComputeObservables() {
    const Index nsamp = vsamp_.rows();
    for (const auto &obname : obsnames_) {
      obsmanager_.Reset(obname);
    }
    for (Index i_samp = 0; i_samp < nsamp; ++i_samp) {
      for (std::size_t i_obs = 0; i_obs < obs_.size(); ++i_obs) {
        const auto &op = obs_[i_obs];
        const auto &name = obsnames_[i_obs];
        obsmanager_.Push(name, ObsLocValue(*op, vsamp_.row(i_samp)).real());
      }
    }
  }

  void Gradient() {
    obsmanager_.Reset("Energy");
    obsmanager_.Reset("EnergyVariance");

    const int nsamp = vsamp_.rows();
    elocs_.resize(nsamp);
    Ok_.resize(nsamp, npar_);

    for (int i = 0; i < nsamp; i++) {
      elocs_(i) = ObsLocValue(ham_, vsamp_.row(i));
      obsmanager_.Push("Energy", elocs_(i).real());

      Ok_.row(i) = psi_.DerLog(vsamp_.row(i));
    }

    elocmean_ = elocs_.mean();
    SumOnNodes(elocmean_);
    elocmean_ /= double(totalnodes_);

    Okmean_ = Ok_.colwise().mean();
    SumOnNodes(Okmean_);
    Okmean_ /= double(totalnodes_);

    Ok_ = Ok_.rowwise() - Okmean_.transpose();

    elocs_ -= elocmean_ * Eigen::VectorXd::Ones(nsamp);

    for (int i = 0; i < nsamp; i++) {
      obsmanager_.Push("EnergyVariance", std::norm(elocs_(i)));
    }

    grad_ = (Ok_.adjoint() * elocs_);

    // Summing the gradient over the nodes
    SumOnNodes(grad_);
    grad_ /= double(totalnodes_ * nsamp);
  }

  /**
   * Computes the value of the local estimator of the operator `ob` in
   * configuration `v` which is defined by O_loc(v) = ⟨v|ob|Ψ⟩ / ⟨v|Ψ⟩.
   *
   * @param ob Operator representing the observable.
   * @param v Many-body configuration
   * @return The value of the local observable O_loc(v).
   */
  Complex ObsLocValue(const AbstractOperator &ob, const Eigen::VectorXd &v) {
    ob.FindConn(v, mel_, connectors_, newconfs_);

    assert(connectors_.size() == mel_.size());

    auto logvaldiffs = (psi_.LogValDiff(v, connectors_, newconfs_));

    assert(mel_.size() == std::size_t(logvaldiffs.size()));

    Complex obval = 0;

    for (int i = 0; i < logvaldiffs.size(); i++) {
      obval += mel_[i] * std::exp(logvaldiffs(i));
    }

    return obval;
  }

  double ElocMean() { return elocmean_.real(); }

  double Elocvar() { return elocvar_; }

  void Advance(Index steps = 1) {
    assert(steps > 0);
    for (Index i = 0; i < steps; ++i) {
      Sample();
      Gradient();
      UpdateParameters();
    }
  }

  Iterator Iterate(const nonstd::optional<Index> &n_iter = nonstd::nullopt,
                   Index step_size = 1) {
    assert(!n_iter.has_value() || n_iter.value() > 0);
    assert(step_size > 0);

    opt_.Reset();
    InitSweeps();

    Advance(step_size);
    return Iterator(*this, step_size, n_iter);
  }

  void Run(const std::string &output_prefix,
           nonstd::optional<Index> n_iter = nonstd::nullopt,
           Index step_size = 1, Index save_params_every = 50) {
    assert(n_iter > 0);
    assert(step_size > 0);
    assert(save_params_every > 0);

    nonstd::optional<JsonOutputWriter> writer;
    if (mynode_ == 0) {
      writer.emplace(output_prefix + ".log", output_prefix + ".wf",
                     save_params_every);
    }
    opt_.Reset();

    for (const auto step : Iterate(n_iter, step_size)) {
      ComputeObservables();

      // Note: This has to be called in all MPI processes, because converting
      // the ObsManager to JSON performs a MPI reduction.
      auto obs_data = json(obsmanager_);
      obs_data["Acceptance"] = sampler_.Acceptance();

      // writer.has_value() iff the MPI rank is 0, so the output is only
      // written once
      if (writer.has_value()) {
        writer->WriteLog(step, obs_data);
        writer->WriteState(step, psi_);
      }
      MPI_Barrier(MPI_COMM_WORLD);
    }
  }

  void UpdateParameters() {
    auto pars = psi_.GetParameters();

    Eigen::VectorXcd deltap(npar_);

    if (dosr_) {
      sr_.ComputeUpdate(Ok_, grad_, deltap);
    } else {
      deltap = grad_;
    }

    opt_.Update(deltap, pars);
    SendToAll(pars);

    psi_.SetParameters(pars);

    MPI_Barrier(MPI_COMM_WORLD);
  }

  void setSrParameters(double diag_shift = 0.01, bool use_iterative = false,
                       bool use_cholesky = true) {
    dosr_ = true;
    sr_.setParameters(diag_shift, use_iterative, use_cholesky,
                      psi_.IsHolomorphic());
  }

  AbstractMachine &GetMachine() { return psi_; }
  const ObsManager &GetObsManager() const { return obsmanager_; }
};

}  // namespace netket

#endif
