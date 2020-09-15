// Copyright (c) 2020 Chris Richardson
// FEniCS Project
// SPDX-License-Identifier:    MIT

#include "nedelec.h"
#include "quadrature.h"
#include "simplex.h"
#include <Eigen/SVD>
#include <numeric>

Nedelec2D::Nedelec2D(int k) : _dim(2), _degree(k - 1)
{
  // Reference triangle
  Eigen::Array<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor> triangle
      = ReferenceSimplex::create_simplex(2);

  // Create orthonormal basis on triangle
  std::vector<Polynomial> Pkp1
      = ReferenceSimplex::compute_polynomial_set(2, _degree + 1);
  int psize = Pkp1.size();

  // Vector subset
  const int nv = (_degree + 1) * (_degree + 2) / 2;

  // PkH subset
  const int ns = _degree + 1;
  const int ns0 = (_degree + 1) * _degree / 2;

  auto [Qpts, Qwts] = make_quadrature(2, 2 * _degree + 2);
  Eigen::Array<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>
      Pkp1_at_Qpts(psize, Qpts.rows());
  for (int j = 0; j < psize; ++j)
    Pkp1_at_Qpts.row(j) = Pkp1[j].tabulate(Qpts);

  // Create initial coefficients of Pkp1.
  Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>
      wcoeffs(nv * 2 + ns, psize * 2);
  wcoeffs.setZero();
  wcoeffs.block(0, 0, nv, nv) = Eigen::MatrixXd::Identity(nv, nv);
  wcoeffs.block(nv, psize, nv, nv) = Eigen::MatrixXd::Identity(nv, nv);

  for (int i = 0; i < ns; ++i)
    for (int k = 0; k < psize; ++k)
    {
      auto w0 = Qwts * Pkp1_at_Qpts.row(ns0 + i).transpose() * Qpts.col(1)
                * Pkp1_at_Qpts.row(k).transpose();
      wcoeffs(2 * nv + i, k) = w0.sum();

      auto w1 = -Qwts * Pkp1_at_Qpts.row(ns0 + i).transpose() * Qpts.col(0)
                * Pkp1_at_Qpts.row(k).transpose();
      wcoeffs(2 * nv + i, k + psize) = w1.sum();
    }

  std::cout << "Initial coeffs = \n[" << wcoeffs << "]\n";

  // Dual space

  Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>
      dualmat(nv * 2 + ns, psize * 2);
  dualmat.setZero();

  // dof counter
  int c = 0;

  // Integral representation for the boundary (edge) dofs

  // Create a polynomial set on a reference edge
  std::vector<Polynomial> Pq
      = ReferenceSimplex::compute_polynomial_set(1, _degree);

  // Create quadrature scheme on the edge
  int quad_deg = 5 * (_degree + 1);
  auto [QptsE, QwtsE] = make_quadrature(1, quad_deg);

  // Iterate over edges
  for (int i = 0; i < 3; ++i)
  {
    // FIXME: get tangent from the simplex class
    Eigen::Array<double, 2, 2, Eigen::RowMajor> edge
        = ReferenceSimplex::sub(triangle, 1, i);
    Eigen::Vector2d tangent = edge.row(1) - edge.row(0);

    // UFC convention?
    if (i == 1)
      tangent *= -1;

    // Map quadrature points onto triangle edge
    Eigen::Array<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>
        QptsE_scaled(QptsE.rows(), 2);
    for (int j = 0; j < QptsE.rows(); ++j)
      QptsE_scaled.row(j)
          = edge.row(0) + QptsE(j, 0) * (edge.row(1) - edge.row(0));

    // Tabulate Pkp1 at edge quadrature points
    Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>
        Pkp1_at_QptsE(psize, QptsE_scaled.rows());
    for (int j = 0; j < psize; ++j)
      Pkp1_at_QptsE.row(j) = Pkp1[j].tabulate(QptsE_scaled);

    // Compute edge tangent integral moments
    for (std::size_t j = 0; j < Pq.size(); ++j)
    {
      Eigen::ArrayXd phi = Pq[j].tabulate(QptsE);
      for (int k = 0; k < 2; ++k)
      {
        Eigen::VectorXd q = phi * QwtsE * tangent[k];
        Eigen::RowVectorXd qcoeffs = Pkp1_at_QptsE * q;
        dualmat.block(c, psize * k, 1, psize) = qcoeffs;
      }
      ++c;
    }
  }

  if (_degree > 0)
  {
    // Interior integral moment
    std::vector<Polynomial> Pkm1
        = ReferenceSimplex::compute_polynomial_set(2, _degree - 1);
    for (std::size_t i = 0; i < Pkm1.size(); ++i)
    {
      Eigen::ArrayXd phi = Pkm1[i].tabulate(Qpts);
      Eigen::VectorXd q = phi * Qwts;
      Eigen::RowVectorXd qcoeffs = Pkp1_at_Qpts.matrix() * q;
      assert(qcoeffs.size() == psize);
      std::cout << "q = [" << q.transpose() << "]\n";
      dualmat.block(c, 0, 1, psize) = qcoeffs;
      ++c;
      dualmat.block(c, psize, 1, psize) = qcoeffs;
      ++c;
    }
  }

  std::cout << "dualmat = \n[" << dualmat << "]\n";

  // See FIAT in finite_element.py constructor
  auto A = wcoeffs * dualmat.transpose();
  auto Ainv = A.inverse();
  auto new_coeffs = Ainv * wcoeffs;
  std::cout << "new_coeffs = \n[" << new_coeffs << "]\n";

  // Create polynomial sets for x and y components
  // stacking x0, x1, x2,... y0, y1, y2,...
  poly_set.resize((nv * 2 + ns) * _dim, Polynomial::zero(2));
  for (int j = 0; j < _dim; ++j)
    for (int i = 0; i < nv * 2 + ns; ++i)
      for (int k = 0; k < psize; ++k)
        poly_set[i + (nv * 2 + ns) * j]
            += Pkp1[k] * new_coeffs(i, k + psize * j);
}
//-----------------------------------------------------------------------------
// Compute basis values at set of points
Eigen::Array<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>
Nedelec2D::tabulate_basis(
    const Eigen::Array<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>&
        pts) const
{
  if (pts.cols() != _dim)
    throw std::runtime_error(
        "Point dimension does not match element dimension");

  Eigen::Array<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor> result(
      pts.rows(), poly_set.size());
  for (std::size_t j = 0; j < poly_set.size(); ++j)
    result.col(j) = poly_set[j].tabulate(pts);

  return result;
}
