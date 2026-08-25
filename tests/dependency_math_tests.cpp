#include <cmath>

#include <Eigen/Core>
#include <Spectra/MatOp/DenseSymMatProd.h>
#include <Spectra/SymEigsSolver.h>
#include <gtest/gtest.h>

TEST(DependencyMath, CurrentEigenAndSpectraComputeSelectedEigenpairs) {
    Eigen::MatrixXd matrix = Eigen::MatrixXd::Zero(6, 6);
    for (Eigen::Index index = 0; index < matrix.rows(); ++index) {
        matrix(index, index) = static_cast<double>(index + 1);
    }

    Spectra::DenseSymMatProd<double> operation(matrix);
    Spectra::SymEigsSolver<Spectra::DenseSymMatProd<double>> solver(operation, 2, 4);
    solver.init();
    solver.compute(Spectra::SortRule::LargestAlge);

    ASSERT_EQ(solver.info(), Spectra::CompInfo::Successful);
    const Eigen::VectorXd values = solver.eigenvalues();
    ASSERT_EQ(values.size(), 2);
#if defined(LAPLACE_TEST_WRONG_EIGENVALUE)
    constexpr double expected_second = 4.0;
#else
    constexpr double expected_second = 5.0;
#endif
    EXPECT_LT(std::abs(values(0) - 6.0), 1.0e-10);
    EXPECT_LT(std::abs(values(1) - expected_second), 1.0e-10);
}
