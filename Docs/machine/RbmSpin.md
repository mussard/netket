# RbmSpin
A fully connected Restricted Boltzmann Machine (RBM). This type of
 RBM has spin 1/2 hidden units and is defined by:

 $$ \Psi(s_1,\dots s_N) = e^{\sum_i^N a_i s_i} \times \Pi_{j=1}^M \cosh
 \left(\sum_i^N W_{ij} s_i + b_j \right) $$

 for arbitrary local quantum numbers $$ s_i $$.

## Class Constructor
Constructs a new ``RbmSpin`` machine:

|    Argument    |         Type         |                                 Description                                  |
|----------------|----------------------|------------------------------------------------------------------------------|
|hilbert         |netket.hilbert.Hilbert|Hilbert space object for the system.                                          |
|n_hidden        |int=0                 |Number of hidden units.                                                       |
|alpha           |int=0                 |Hidden unit density.                                                          |
|use_visible_bias|bool=True             |If ``True`` then there would be a bias on the visible units. Default ``True``.|
|use_hidden_bias |bool=True             |If ``True`` then there would be a bias on the visible units. Default ``True``.|

### Examples
A ``RbmSpin`` machine with hidden unit density
alpha = 2 for a one-dimensional L=20 spin-half system:

```python
>>> from netket.machine import RbmSpin
>>> from netket.hilbert import Spin
>>> from netket.graph import Hypercube
>>> g = Hypercube(length=20, n_dim=1)
>>> hi = Spin(s=0.5, total_sz=0, graph=g)
>>> ma = RbmSpin(hilbert=hi,alpha=2)
>>> print(ma.n_par)
860

```



## Class Methods 
### der_log
Member function to obtain the derivatives of log value of
machine given an input wrt the machine's parameters.

|Argument|            Type            |      Description       |
|--------|----------------------------|------------------------|
|v       |numpy.ndarray[float64[m, 1]]|Input vector to machine.|

### init_random_parameters
Member function to initialise machine parameters.

|Argument|  Type   |                               Description                                |
|--------|---------|--------------------------------------------------------------------------|
|seed    |int=1234 |The random number generator seed.                                         |
|sigma   |float=0.1|Standard deviation of normal distribution from which parameters are drawn.|

### load
Member function to load machine parameters from a json file.

|Argument|Type|             Description             |
|--------|----|-------------------------------------|
|filename|str |name of file to load parameters from.|

### log_val
Member function to obtain log value of machine given an input
vector.

|Argument|            Type            |      Description       |
|--------|----------------------------|------------------------|
|v       |numpy.ndarray[float64[m, 1]]|Input vector to machine.|

### log_val_diff
Member function to obtain difference in log value of machine
given an input and a change to the input.

|Argument|            Type            |                                 Description                                 |
|--------|----------------------------|-----------------------------------------------------------------------------|
|v       |numpy.ndarray[float64[m, 1]]|Input vector to machine.                                                     |
|tochange|List[List[int]]             |list containing the indices of the input to be changed                       |
|newconf |List[List[float]]           |list containing the new (changed) values at the indices specified in tochange|

### save
Member function to save the machine parameters.

|Argument|Type|            Description            |
|--------|----|-----------------------------------|
|filename|str |name of file to save parameters to.|

## Properties

|   Property   |         Type         |                                                   Description                                                    |
|--------------|----------------------|------------------------------------------------------------------------------------------------------------------|
|hilbert       |netket.hilbert.Hilbert| The hilbert space object of the system.                                                                          |
|is_holomorphic|bool                  | Whether the given wave-function is a holomorphic function of             its parameters                          |
|n_par         |int                   | The number of parameters in the machine.                                                                         |
|n_visible     |int                   | The number of inputs into the machine aka visible units in             the case of Restricted Boltzmann Machines.|
|parameters    |list                  | List containing the parameters within the layer.             Read and write                                      |
