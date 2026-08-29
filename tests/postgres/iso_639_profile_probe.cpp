#include "../iso_639_profile_fixture.hpp"
#include "tabular_profile_probe.hpp"

int main(int argc, char** argv) {
    return laplace::test::RunTabularProfileProbe<
        laplace::test::iso_profile::Profile,
        laplace::test::Iso639ProfileFixture>(argc, argv, "ISO", "ISO-639");
}
