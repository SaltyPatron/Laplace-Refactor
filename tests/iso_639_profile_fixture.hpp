#ifndef LAPLACE_TEST_ISO_639_PROFILE_FIXTURE_HPP
#define LAPLACE_TEST_ISO_639_PROFILE_FIXTURE_HPP

#include "laplace/source/iso_639_3_20260415_profile.h"
#include "tabular_profile_fixture.hpp"

namespace laplace::test {

namespace iso_profile = generated::iso_639_3_20260415;
using Iso639ProfileFixture = TabularProfileFixture<iso_profile::Profile>;

}  // namespace laplace::test

#endif
