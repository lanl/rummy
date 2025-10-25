# Rummy

A flexible input deck parser and compiler. Rummy reads and compiles input decks in the Parthenon/Athena++ style:
```
<suit1>
card1 = value1
<suit1>
card2 = value2
```
The names enclosed in `<>` are the "suits" and the variables in the suits are "cards". 
An input deck is defined as a collection of card name + value pairs.
In Rummy, the entire input deck is parsed and compiled from the top down with a simple stack-based, bytecode compiler. 
This allows, among other things, the ability to set card values with expressions and refer to other cards as variables in those expressions.
An example input deck that demonstrates several of the capabilities of Rummy:
```shell
# Global variables
L = 1.0, 1.0, 0.5                     # vectors
n = [10, 10, 1]
xmin = -L[0]/2., -L[1]/2., -L[2]/2.   # reference other variables
xmax[:] =  0.5 * L[:3]                # vector expressions 
rho = 1.0
hcond = 0.1
name = "heat_conduction"              # string variables

<physics>
hydro = true                          # boolean variables
conduction = true

<outputs>
vars = "density", "velocity", "pressure" 

<mesh>
 xmin[:] = xmin[:3]                   # vector slices
 xmax[:] = xmax[:3]
 nx = n[0]
 ny = n[1]

<gas>
 name = "hydrogen"

<../eos>                              # relative nodes, expands to gas/eos
 type = "ideal"         
 gamma = 5.0/3.0
 cv = 1.0/(gamma - 1.0)               # Reference local variables in your node

<../conductivity>
 kappa = hcond/(rho * gas.eos.cv)     # reference other node variables 
 
<hydro>
 cfl = 0.8
<../riemann>
 solver = "hllc"

print(gas.conductivity.kappa)         # Print variables to stdout when the input deck is compiled
```

# Features

* Full math expression support
* `**` power, `//` integer division operators and `pi` named constant. 
* Multiline expressions
* Vector expressions
* Vector slice operations
* String addition
* Boolean logical operations
* Relative suits (`<../subnode>`)
* Global cards (i.e., no suit)
* A `print` function that can print any previously defined card.
* Error messages. 

# Including

Including Rummy in another project is as simple as building the code with CMake and including the `rummy/deck.hpp` header file. 
An example implementation that builds a new deck is,

```c++
#include <memory>
#include <string>

#include <rummy/deck.hpp>

void ReadInputDeck(std::string fname) {

  auto deck = std::make_unique<Rummy::Deck>();
  deck->Build(fname);
}
```
`Build` can be called on a file name or a `std::stringstream` object. 
The standard `GetCard`, `UpdateCard`, `AddCard` functions are available for retrieving and setting cards.
Cards are stored in a two-layer map, `deck[suit][card]`, for simple traversal. 

# The Compiler

The compiler was written while following the "Crafting Interpreters" book by Robert Nystrom. The compiler in that book is written in C and is meant to be a complete programming language with branch statements, loop statements, functions, and classes. 
The compiler that is in Rummy was converted from C to C++ on the fly and simplified in many areas. 
For example, strings in Rummy are not allocated linked-lists but instead are simple stack allocated character arrays. 
There are some unused features of the compiler that were not removed such as `if-else` statements and `for` loops. These may or may not work as is, but there are no plans to fully support them. 
The compiler itself is header only, and so can be easily dropped into other codes without the Parthenon/Athena++ frontend parser.


# Release

Rummy is released under the BSD 3-Clause License with release number O4997. For more details see the LICENSE.md file.
