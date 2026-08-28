#include "../cili_profile_fixture.hpp"
#include "tabular_profile_probe.hpp"

int main(int argc, char** argv) {
    return laplace::test::RunTabularProfileProbe<
        laplace::test::cili_profile::Profile,
        laplace::test::CiliProfileFixture>(argc, argv, "CILI", "CILI");
}
