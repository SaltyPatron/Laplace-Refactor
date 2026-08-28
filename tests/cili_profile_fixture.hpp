#ifndef LAPLACE_TEST_CILI_PROFILE_FIXTURE_HPP
#define LAPLACE_TEST_CILI_PROFILE_FIXTURE_HPP

#include "laplace/source/cili_pwn_mappings_20240611_profile.h"
#include "tabular_profile_fixture.hpp"

namespace laplace::test {

namespace cili_profile = generated::cili_pwn_mappings_20240611;
using CiliProfileFixture = TabularProfileFixture<cili_profile::Profile>;

}  // namespace laplace::test

#endif
