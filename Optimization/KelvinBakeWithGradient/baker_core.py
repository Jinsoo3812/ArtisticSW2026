"""
baker_core.py - Core Kelvin Wake Reference Evaluator with Analytical Gradient Baker
Based on:
  1. Darmon, Benzaquen, Raphael (2013/2014) "Kelvin wake pattern at large Froude numbers",
     J. Fluid Mech. 738, R3 (arXiv:1309.6751).
  2. Marc Bresson (2022) "Kelvin Wake Pattern Simulation"
     (https://github.com/MarcBresson/kelvin-wake-pattern-simulation).

Mathematical Formulation:
  Dimensionless coordinates:
    u = x / lambda   (downstream coordinate behind apex, u >= 0)
    v = y / lambda   (lateral coordinate, v in [-v_max, +v_max])
    where lambda = 2 * pi * V^2 / g = 2 * pi * Fr^2 * b

  Surface elevation integral (Eq. 8 in Darmon et al. 2013):
    zeta(u, v, Fr) = -2 * integral_0^{pi/2} [ P_hat(theta, Fr) / cos^4(theta) ]
                     * sin( 2 * pi * u / cos(theta) )
                     * cos( 2 * pi * v * sin(theta) / cos^2(theta) ) dtheta

    where P_hat(theta, Fr) = exp( - 1 / (4 * pi^2 * Fr^4 * cos^4(theta)) )

  Analytical Gradient Integrals (Exact Derivatives w.r.t u and v):
    dzeta / du = -4 * pi * integral_0^{pi/2} [ P_hat(theta, Fr) / cos^5(theta) ]
                 * cos( 2 * pi * u / cos(theta) )
                 * cos( 2 * pi * v * sin(theta) / cos^2(theta) ) dtheta

    dzeta / dv = +4 * pi * integral_0^{pi/2} [ P_hat(theta, Fr) * sin(theta) / cos^6(theta) ]
                 * sin( 2 * pi * u / cos(theta) )
                 * sin( 2 * pi * v * sin(theta) / cos^2(theta) ) dtheta
"""

import os
import site
import sys

user_site = site.getusersitepackages()
if os.path.exists(user_site) and user_site not in sys.path:
    sys.path.insert(0, user_site)

import numpy as np
import scipy.integrate as integ
from dataclasses import dataclass
from typing import Tuple, Optional, Union, List


# Physical constants
KELVIN_CUSP_ANGLE_DEG = 19.47122  # arcsin(1/3) in degrees
KELVIN_CUSP_ANGLE_RAD = np.arcsin(1.0 / 3.0)


def pressure_spectrum(theta: Union[float, np.ndarray], Fr: float) -> Union[float, np.ndarray]:
    """
    Computes Gaussian hull pressure Fourier spectrum P_hat(theta, Fr).
    K0(theta) = 1 / (Fr^2 * cos^2(theta))
    P_hat = exp( - K0^2 / (4 * pi^2) ) = exp( - 1 / (4 * pi^2 * Fr^4 * cos^4(theta)) )
    """
    c = np.cos(theta)
    K0_inv = (Fr * c)**2  # Fr^2 * cos^2(theta)
    
    denom = 4.0 * (np.pi ** 2) * (K0_inv ** 2)
    with np.errstate(divide='ignore', over='ignore', invalid='ignore'):
        arg = -1.0 / denom
        p_hat = np.exp(np.clip(arg, -500.0, 0.0))
        if isinstance(p_hat, np.ndarray):
            p_hat = np.nan_to_num(p_hat, nan=0.0, posinf=0.0, neginf=0.0)
        else:
            if np.isnan(p_hat):
                p_hat = 0.0
    return p_hat


class ReferenceAdaptiveEvaluator:
    """
    High-precision point-wise reference evaluator using scipy.integrate.quad.
    Used as golden ground truth for precision validation of height and analytical gradients.
    """
    def __init__(self, epsabs: float = 1e-8, epsrel: float = 1e-8, limit: int = 2000):
        self.epsabs = epsabs
        self.epsrel = epsrel
        self.limit = limit

    def evaluate_height(self, u: float, v: float, Fr: float) -> float:
        """Calculates dimensionless surface elevation zeta(u, v, Fr)."""
        if u < 0.0:
            return 0.0

        def integrand(theta: float) -> float:
            c = np.cos(theta)
            if c < 1e-7:
                return 0.0
            c2 = c * c
            c4 = c2 * c2
            p_hat = pressure_spectrum(theta, Fr)
            if p_hat == 0.0:
                return 0.0
            
            s = np.sin(theta)
            sin_term = np.sin(2.0 * np.pi * u / c)
            cos_term = np.cos(2.0 * np.pi * v * s / c2)
            return (p_hat / c4) * sin_term * cos_term

        val, _ = integ.quad(
            integrand,
            0.0,
            np.pi / 2.0 - 1e-8,
            epsabs=self.epsabs,
            epsrel=self.epsrel,
            limit=self.limit
        )
        return float(-2.0 * val)

    def evaluate_gradients(self, u: float, v: float, Fr: float) -> Tuple[float, float]:
        """Calculates analytical derivatives (dzeta/du, dzeta/dv)."""
        if u < 0.0:
            return 0.0, 0.0

        def integrand_u(theta: float) -> float:
            c = np.cos(theta)
            if c < 1e-7:
                return 0.0
            c2 = c * c
            c5 = c2 * c2 * c
            p_hat = pressure_spectrum(theta, Fr)
            if p_hat == 0.0:
                return 0.0
            s = np.sin(theta)
            cos_u = np.cos(2.0 * np.pi * u / c)
            cos_v = np.cos(2.0 * np.pi * v * s / c2)
            return (p_hat / c5) * cos_u * cos_v

        def integrand_v(theta: float) -> float:
            c = np.cos(theta)
            if c < 1e-7:
                return 0.0
            c2 = c * c
            c6 = c2 * c2 * c2
            p_hat = pressure_spectrum(theta, Fr)
            if p_hat == 0.0:
                return 0.0
            s = np.sin(theta)
            sin_u = np.sin(2.0 * np.pi * u / c)
            sin_v = np.sin(2.0 * np.pi * v * s / c2)
            return (p_hat * s / c6) * sin_u * sin_v

        val_u, _ = integ.quad(integrand_u, 0.0, np.pi / 2.0 - 1e-8, epsabs=self.epsabs, epsrel=self.epsrel, limit=self.limit)
        val_v, _ = integ.quad(integrand_v, 0.0, np.pi / 2.0 - 1e-8, epsabs=self.epsabs, epsrel=self.epsrel, limit=self.limit)
        return float(-4.0 * np.pi * val_u), float(4.0 * np.pi * val_v)


class GaussLegendreVectorizedEvaluator:
    """
    Composite/Piecewise Gauss-Legendre Quadrature Evaluator for fast, high-resolution grid generation.
    Computes Height (Z), Gradient U (dZ/du), and Gradient V (dZ/dv) simultaneously in vectorized chunks.
    """
    def __init__(self, n_sub: int = 8, n_nodes_per_sub: int = 128, n_nodes: Optional[int] = None):
        if n_nodes is not None:
            n_sub = 8
            n_nodes_per_sub = max(32, n_nodes // 8)
            
        self.n_sub = n_sub
        self.n_nodes_per_sub = n_nodes_per_sub
        
        eps = 1e-7
        b_upper = np.pi / 2.0 - eps
        theta_bounds = np.linspace(0.0, b_upper, n_sub + 1)
        base_nodes, base_weights = np.polynomial.legendre.leggauss(n_nodes_per_sub)
        
        all_theta = []
        all_weights = []
        for a, b in zip(theta_bounds[:-1], theta_bounds[1:]):
            th_m = 0.5 * (base_nodes + 1.0) * (b - a) + a
            w_m = base_weights * 0.5 * (b - a)
            all_theta.append(th_m)
            all_weights.append(w_m)
            
        self.theta = np.concatenate(all_theta)
        self.weights = np.concatenate(all_weights)
        self.total_nodes = len(self.theta)
        
        self.cos_theta = np.cos(self.theta)
        self.sin_theta = np.sin(self.theta)
        self.cos2_theta = self.cos_theta ** 2
        self.cos4_theta = self.cos2_theta ** 2
        self.cos5_theta = self.cos4_theta * self.cos_theta
        self.cos6_theta = self.cos4_theta * self.cos2_theta

    def evaluate_grid(
        self,
        u_grid: np.ndarray,
        v_grid: np.ndarray,
        Fr: float,
        chunk_size: int = 50000
    ) -> np.ndarray:
        """Evaluates surface elevation zeta(u, v) on 2D coordinate grids."""
        orig_shape = u_grid.shape
        u_flat = u_grid.ravel()
        v_flat = v_grid.ravel()
        n_points = len(u_flat)

        p_hat = pressure_spectrum(self.theta, Fr)
        node_factor = (p_hat / self.cos4_theta) * self.weights

        zeta_flat = np.zeros(n_points, dtype=np.float64)

        for start_idx in range(0, n_points, chunk_size):
            end_idx = min(start_idx + chunk_size, n_points)
            u_chunk = u_flat[start_idx:end_idx, None]
            v_chunk = v_flat[start_idx:end_idx, None]

            u_phase = 2.0 * np.pi * u_chunk / self.cos_theta[None, :]
            v_phase = 2.0 * np.pi * v_chunk * self.sin_theta[None, :] / self.cos2_theta[None, :]

            integrand = np.sin(u_phase) * np.cos(v_phase) * node_factor[None, :]
            zeta_flat[start_idx:end_idx] = -2.0 * np.sum(integrand, axis=1)

        zeta_flat[u_flat < 0.0] = 0.0
        return zeta_flat.reshape(orig_shape)

    def evaluate_gradients(
        self,
        u_grid: np.ndarray,
        v_grid: np.ndarray,
        Fr: float,
        chunk_size: int = 50000
    ) -> Tuple[np.ndarray, np.ndarray]:
        """Evaluates analytical derivatives dzeta/du and dzeta/dv on 2D coordinate grids."""
        orig_shape = u_grid.shape
        u_flat = u_grid.ravel()
        v_flat = v_grid.ravel()
        n_points = len(u_flat)

        p_hat = pressure_spectrum(self.theta, Fr)
        factor_u = (p_hat / self.cos5_theta) * self.weights
        factor_v = (p_hat * self.sin_theta / self.cos6_theta) * self.weights

        dz_du_flat = np.zeros(n_points, dtype=np.float64)
        dz_dv_flat = np.zeros(n_points, dtype=np.float64)

        for start_idx in range(0, n_points, chunk_size):
            end_idx = min(start_idx + chunk_size, n_points)
            u_chunk = u_flat[start_idx:end_idx, None]
            v_chunk = v_flat[start_idx:end_idx, None]

            u_phase = 2.0 * np.pi * u_chunk / self.cos_theta[None, :]
            v_phase = 2.0 * np.pi * v_chunk * self.sin_theta[None, :] / self.cos2_theta[None, :]

            cos_u = np.cos(u_phase)
            sin_u = np.sin(u_phase)
            cos_v = np.cos(v_phase)
            sin_v = np.sin(v_phase)

            integrand_u = cos_u * cos_v * factor_u[None, :]
            dz_du_flat[start_idx:end_idx] = -4.0 * np.pi * np.sum(integrand_u, axis=1)

            integrand_v = sin_u * sin_v * factor_v[None, :]
            dz_dv_flat[start_idx:end_idx] = 4.0 * np.pi * np.sum(integrand_v, axis=1)

        dz_du_flat[u_flat < 0.0] = 0.0
        dz_dv_flat[u_flat < 0.0] = 0.0

        return dz_du_flat.reshape(orig_shape), dz_dv_flat.reshape(orig_shape)

    def evaluate_all(
        self,
        u_grid: np.ndarray,
        v_grid: np.ndarray,
        Fr: float,
        chunk_size: int = 50000
    ) -> Tuple[np.ndarray, np.ndarray, np.ndarray]:
        """Simultaneously evaluates Height (Z), Gradient U (dZ/du), and Gradient V (dZ/dv)."""
        orig_shape = u_grid.shape
        u_flat = u_grid.ravel()
        v_flat = v_grid.ravel()
        n_points = len(u_flat)

        p_hat = pressure_spectrum(self.theta, Fr)
        factor_z = (p_hat / self.cos4_theta) * self.weights
        factor_u = (p_hat / self.cos5_theta) * self.weights
        factor_v = (p_hat * self.sin_theta / self.cos6_theta) * self.weights

        z_flat = np.zeros(n_points, dtype=np.float64)
        dz_du_flat = np.zeros(n_points, dtype=np.float64)
        dz_dv_flat = np.zeros(n_points, dtype=np.float64)

        for start_idx in range(0, n_points, chunk_size):
            end_idx = min(start_idx + chunk_size, n_points)
            u_chunk = u_flat[start_idx:end_idx, None]
            v_chunk = v_flat[start_idx:end_idx, None]

            u_phase = 2.0 * np.pi * u_chunk / self.cos_theta[None, :]
            v_phase = 2.0 * np.pi * v_chunk * self.sin_theta[None, :] / self.cos2_theta[None, :]

            sin_u = np.sin(u_phase)
            cos_u = np.cos(u_phase)
            sin_v = np.sin(v_phase)
            cos_v = np.cos(v_phase)

            integrand_z = sin_u * cos_v * factor_z[None, :]
            integrand_u = cos_u * cos_v * factor_u[None, :]
            integrand_v = sin_u * sin_v * factor_v[None, :]

            z_flat[start_idx:end_idx] = -2.0 * np.sum(integrand_z, axis=1)
            dz_du_flat[start_idx:end_idx] = -4.0 * np.pi * np.sum(integrand_u, axis=1)
            dz_dv_flat[start_idx:end_idx] = 4.0 * np.pi * np.sum(integrand_v, axis=1)

        z_flat[u_flat < 0.0] = 0.0
        dz_du_flat[u_flat < 0.0] = 0.0
        dz_dv_flat[u_flat < 0.0] = 0.0

        return z_flat.reshape(orig_shape), dz_du_flat.reshape(orig_shape), dz_dv_flat.reshape(orig_shape)


@dataclass
class KelvinDomainConfig:
    u_min: float = 0.0
    u_max: float = 10.0
    v_min: float = -3.0
    v_max: float = +3.0
    res_u: int = 512
    res_v: int = 256

    def get_coordinate_grids(self) -> Tuple[np.ndarray, np.ndarray]:
        u_lin = np.linspace(self.u_min, self.u_max, self.res_u, dtype=np.float64)
        v_lin = np.linspace(self.v_min, self.v_max, self.res_v, dtype=np.float64)
        return np.meshgrid(u_lin, v_lin, indexing='ij')
