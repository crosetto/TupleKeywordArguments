# Tuple, move semantic and perfect forwarding

This example covers few topics:
1. how to implement your own tuple (which might be useful in cases where you cannot use the std::tuple,
   like when compiling CUDA kernels with nvcc)
2. how to build a "python-like" keyword-argument initialization for the tuple, with zero runtime overhead, which allows to initialize tuple objects with
   the arguments out of order, and skipping some of the elements (which are default-initialized).
3. shows with a practical example why we need perfect forwarding and forwarding references.
4. shows once again the use (and the power) of constexpr: this tuple can be instantiated as a compile time constant, just like std::tuple.

This example turns out to be a great opportunity to understand the caveats of move semantics and
perfect forwarding: we want to be able to store move-only objects in our tuple, like a 
std::unique_ptr, and whenever we set a new value for that element we want the move 
constructor/assignment to be called.

Let's say that we want to instantiate an object
with three integer "coordinates", a unique_ptr to double, a string name, a boolean flag (which might be representing an object in a simulation,
like a particle or a vehicle).
Would be a tuple
```C++
using type=std::tuple<int, int, int, std::unique_ptr<double>, std:string, bool>;
```
But say we don't want necessarily to initialize all the arguments at construction, and we need this API:
```C++
using name=arg<5>;
using y=arg<2>;
using z=arg<3>;
using t=tuple<int, int, int, std::unique_ptr<double>, std:string, bool>
auto t1=t{name("Object1"), y(3)};
auto t2=t{name("Object2"), z(6)};
auto t3=t{arg<4>(std::make_unique<double>())};
```
We implement this kind of tuple from scratch, and we can do it in relatively few lines of code.

DISCLAIMER: this code is not safe for use in production, since I omitted all the protections for the sake of clarity.

## Tuple

We start by defining our own tuple. To this end:
* we nest objects using inheritance: for a tuple of size N, we'll have N objects inheriting from each other
* each object contains an element of templated type T, called m_value
* any object can access all the m_values in the parents by fully qualifying them (i.e. super::super::super::m_value)
* our final tuple is the last child, which is inheriting from all other objects

We start by the definition of our tuple: it has a first integral parameter containing the dimension,
 and then the list of types contained in the tuple.
```C++
template <int NDim, typename... Types> struct sized_tuple;
```
The tuple dimension gets forwarded to all the hierarchy of objects, and it is used to compute the "s_index"
(the position of the element in the tuple), as shown below. We have two
template specializations: one for the generic case, and one for the empty tuple
(which represents the first parent in the hierarchy, and stops all template recursions).
The generic case looks as follows (we omit the constructor for the moment)

```C++
template <int NDim, typename First, typename... Types>
struct sized_tuple<NDim, First, Types...>
	: public sized_tuple<NDim, Types...> {

private:
	static const int s_index = NDim - sizeof...( Types );

public:
	typedef sized_tuple<NDim, Types...> super;

	template <int Idx>
	constexpr auto&& get() const
	{
		if constexpr( Idx == s_index )
			return std::move(m_value);
		else
			return super::template get<Idx>();
	}

	template <int Idx, typename T>
	void set( T && arg_ )
	{
		access<sized_tuple, Idx>::type::m_value = std::move(arg_);
	}

protected:
	First m_value;
};
```

while the case stopping the recursion (not inheriting from anything)
```C++
// recursion anchor
template <int NDim> struct sized_tuple<NDim> {
  template <typename... GenericElements>
  constexpr sized_tuple(GenericElements &&...) {}

  static const int s_index = NDim + 1;

  // never called
  template <int Idx> constexpr int get() const { return 0; }

  template <int Idx, typename T> void set(T const &arg_) {}
};
```

Notice the presence of std::move in a couple of occasion: that's in order to
call move constructors (when defined) instead of copy constructors,
so that we can handle move-only objects (like std::unique_ptr, std::future) as well.
This will become clearer later on.

We left undefined the "access" metafunction, which is used like the
operator[] in an array: given an index N it returns the value contained
in the Nth element. In practice it concatenates ```::super``` N times.

```C++
template <typename T, int Idx> struct access {
  using type = typename access<typename T::super, Idx - 1>::type;
};

template <typename T> struct access<T, 1> { using type = T; };
```

## Keyword arguments

We define a structure to associate the position to the value stored
in the tuple. It is like an std::pair, attaching an integer argument to a tuple member:

```C++
template <int Index, typename Type>
struct position {
	using value_type = Type;

	template <typename TT>
	constexpr position( TT&& val )
		: value{ val }
	{
	}

	static constexpr int index = Index;
	Type& value;
};
```

The peculiarity of this class is that its constructor accepts a "forwarding reference", which might
be an rvalue or lvalue reference. The argument gets stored into a reference
data member (which is an lvalue reference), and thus in case it is an rvalue reference its
lifetime gets extended. We need this because we want to handle move-only objects created on the fly
as well as regular lvalues with a single constructor:

* If the constructor is accepting regular references, we get an error when we try to pass in an
  object constructed on the fly.
* if we have only a move constructor (``` constexpr position( Type&& val ) ```) we cannot
  pass in lvalue references.

We define a handy function just to get a more friendly API
```C++
template <int Index, typename Type>
auto constexpr pos( Type&& value )
{
	return position<Index, Type>( std::forward<Type>(value) );
}
```

Now, this was pretty clear hopefully.
all the complication goes into the tuple constructor: we need a way to initialize only some of the
elements and out of order.

To this end we implement a helper function, to which we give the original name "initialize":
a function we call for every
elementin the tuple. It is a regular
recursive constexpr function, "iterating" over the arguments passed to the tuple constructor.
If the numeric position (s_index) of the given tuple element matches with the index of one of the arguments,
 the element gets initialized with that value. Otherwise the element is initialized with the default
 constructor.
This would be extremely inefficient if we had to execute it at run time:
a quadratic algorithm to initialize all the elements in the tuple.
But the recursive function is declared constexpr, and the recursion happens at compile time.
There is no runtime overhead in initializing the arguments this way.

```C++
//recursion anchor
template <int N, typename Element>
constexpr auto initialize() { return Element{}; }

//generic case
template <int N, typename Element, typename X, typename... Rest>
constexpr auto initialize( X&& x, Rest&&... rest )
{
	if constexpr( std::remove_reference<X>::type::index == N )
		return std::move( x.value );
	else
		return initialize<N, Element>( rest... );
}
```

We can eventually define the constructor for the generic tuple:

```C++
	template <typename... GenericElements>
	constexpr sized_tuple( GenericElements&&... x )
		: super( std::forward<GenericElements>( x )... )
		, m_value( initialize<s_index, First>( std::forward<GenericElements>( x )... ) )
	{
	}
```

Now what's left is to define a user friendly type alias for our sized_tuple, which meets the target API
```C++
template <typename... T>
using tuple = sized_tuple<sizeof...( T ), T...>;
```

And we can use it as follows

```C++
	constexpr tuple<bool, char, double, int> ctuple( pos<1>( false ), pos<3>( 3.14 ), pos<2>( 'm' ) );
	static_assert( ctuple.get<1>() == false, "error" );
	static_assert( ctuple.get<2>() == 'm', "error" );
	static_assert( ctuple.get<3>() == 3.14, "error" );
	static_assert( ctuple.get<4>() == int(), "error" );

	tuple<bool, short, std::unique_ptr<std::string>>
		tuple2( pos<1>( false ), pos<2>( 4 ),
				pos<3>( std::make_unique<std::string>( std::string( "pink pig" ) ) )
		);

	tuple2.set<3>(std::make_unique<std::string>(std::string("black dog")));
	std::cout << *tuple2.get<3>() << std::endl;
```

We can check the "zero overhead" by making up a tuple with several members, say
```C++
	tuple<int, int, int
		  ,double*
		  ,std::unique_ptr<std::string>
		  ,bool
		  >
		tuple_( pos<4>( storage2 ) );

	volatile auto tmp = tuple_.get<4>();
```
by compiling it with optimization on, running it on GDB until the element access and showing the assembly generated (command ```layout split``` in GDB)

```
> g++ -O3 -g main.cpp
> gdb a.out
(gdb) break main.cpp:<linenumber>
(gdb) run
(gdb) layout split
```
One sees that both initializing the tuple and accessing the element at position 4 result in a single ```mov``` instruction, e.g.:
```
mov    %rax,0x8(%rsp)
```
