#include <iostream>
#include <memory>
#include <tuple>

template <int NDim, typename... Types>
struct sized_tuple;

// recursion anchor
template <int NDim>
struct sized_tuple<NDim> {
	template <typename... GenericElements>
	constexpr sized_tuple( GenericElements &&... ) {}

	static const int s_index = NDim + 1;

	// never called
	template <int Idx>
	constexpr int get() const { return 0; }

	template <int Idx, typename T>
	void set( T&& arg_ ) {}
};

template <int Index, typename Type>
struct position {
	using value_type = Type;

	template <typename TT>
	constexpr position( TT&& val )
		: value{ val }
	{
	}
	//constexpr position(Type const& val) : value{val} {}

	static constexpr int index = Index;
	Type&				 value;
};

template <int N, typename First>
constexpr auto initialize() { return First{}; }

template <int N, typename First, typename X, typename... Rest>
constexpr auto initialize( X&& x, Rest&&... rest )
{
	if constexpr( std::remove_reference<X>::type::index == N )
		return std::move( x.value );
	else
		return initialize<N, First>( rest... );
}

template <typename T, int Idx>
struct access {
	using type = typename access<typename T::super, Idx - 1>::type;
};

template <typename T>
struct access<T, 1> {
	using type = T;
};

template <int NDim, typename First, typename... Types>
struct sized_tuple<NDim, First, Types...>
	: public sized_tuple<NDim, Types...> {

private:
	static const int s_index = NDim - sizeof...( Types );

public:
	typedef sized_tuple<NDim, Types...> super;

	template <typename... GenericElements>
	constexpr sized_tuple( GenericElements&&... x )
		: super( std::forward<GenericElements>( x )... )
		, m_value( initialize<s_index, First>( std::forward<GenericElements>( x )... ) )
	{
	}

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

template <int Index, typename Type>
auto constexpr pos( Type&& value )
{
	return position<Index, Type>( std::forward<Type>(value) );
}

template <typename... T>
using tuple = sized_tuple<sizeof...( T ), T...>;

int main()
{

	// constexpr tuple<bool, std::unique_ptr<char>, double, int> ctuple( pos<1>( false ), pos<3>( 3.14 ),
	// 												 pos<2>( std::make_unique<char>('m') ) );
	constexpr tuple<bool, char, double, int> ctuple( pos<1>( false ), pos<3>( 3.14 ),
													 pos<2>( 'm' ) );
	static_assert( ctuple.get<1>() == false, "error" );
	static_assert( ctuple.get<2>() == 'm', "error" );
	static_assert( ctuple.get<3>() == 3.14, "error" );
	static_assert( ctuple.get<4>() == int(), "error" );
	//    static_assert(c_tuple.get<4>()=='b', "error");  // This trigger an error

	tuple<bool, short
		  //, std::string
		  ,
		  std::unique_ptr<std::string>
		  >
		tuple2( pos<1>( false ), pos<2>( 4 ),
				pos<3>( std::make_unique<std::string>( std::string( "pink pig" ) ) )
				//pos<3>(std::string("pink pig"))
		);

	auto p = std::make_unique<std::string>(std::string("black dog"));
	tuple2.set<3>(p);

	std::cout << *tuple2.get<3>() << std::endl;


	double d=3.;
	auto storage1 = &d;
	auto storage2 = &d;

	auto p4 = pos<4>( storage2 );
	tuple<int, int, int
		  ,double*
		  ,std::unique_ptr<std::string>
		  ,bool
		  >
		tuple3( p4 );

	volatile auto tmp = storage1;
	volatile auto tmp2 = tuple3.get<4>();

	tuple3.set<5>(std::make_unique<std::string>("ciao\n"));
	auto&& tmp3 = tuple3.get<5>();
	std::cout<<*tmp3<<"\n";
}
