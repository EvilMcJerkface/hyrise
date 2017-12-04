#include <iostream>

#include <boost/hana/map.hpp>
#include <boost/hana/type.hpp>
#include <boost/hana/tuple.hpp>
#include <boost/hana/pair.hpp>
#include <boost/hana/at_key.hpp>
#include <boost/hana/integral_constant.hpp>

#include <type_traits>

namespace opossum {

template <typename EnumType>
struct enum_constant_tag {
  static_assert(std::is_enum_v<EnumType>, "EnumType must be an enum (class).");

  using value_type = EnumType;
};

template <typename EnumType, EnumType enum_value>
struct enum_constant {
  static_assert(std::is_enum_v<EnumType>, "EnumType must be an enum (class).");

  using hana_tag = enum_constant_tag<EnumType>;

  static constexpr auto value = enum_value;
};

template <auto enum_value>
[[maybe_unused]] constexpr auto enum_c = enum_constant<decltype(enum_value), enum_value>{};

template <typename T>
struct EnumConstant : std::false_type {};

template <typename EnumType>
struct EnumConstant<enum_constant_tag<EnumType>> : std::true_type {};

}  // namespace opossum


namespace boost::hana {

template <typename E>
struct value_impl<E, when<opossum::EnumConstant<E>::value>> {
    template <typename C>
    static constexpr auto apply() { return C::value; }
};

template <typename Tag>
struct hash_impl<Tag, when<opossum::EnumConstant<Tag>::value>> {
  template <typename X>
  static constexpr auto apply(const X& x) {
    return type_c<opossum::enum_constant<decltype(X::value), X::value>>;
  }
};

}  // namespace boost::hana

enum class DataType { Int, Long, String };

struct A { static constexpr int value = 0u; };
struct B { static constexpr int value = 1u; };
struct C { static constexpr int value = 2u; };

namespace hana = boost::hana;

constexpr auto class_for_data_type = hana::make_map(
  hana::make_pair(opossum::enum_c<DataType::Int>, hana::type_c<A>),
  hana::make_pair(opossum::enum_c<DataType::Long>, hana::type_c<B>),
  hana::make_pair(opossum::enum_c<DataType::String>, hana::type_c<C>));


namespace hana = boost::hana;

int main() {
  // auto enum_value = opossum::enum_c<DataType::Int>;

  auto enum_obj = opossum::enum_c<DataType::String>;

  auto enum_value = decltype(enum_obj)::value;

  auto a_obj = hana::at_key(class_for_data_type, opossum::enum_c<DataType::String>);
  using ObjType = typename decltype(a_obj)::type;

  std::cout << static_cast<int>(enum_value) << ObjType::value << hana::Constant<opossum::enum_constant<DataType, DataType::Int>>::value << std::endl;
  return 0;
}
