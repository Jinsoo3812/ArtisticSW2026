# Kelvin Wake Analytical Gradient Validation Summary

**Overall Status**: ✅ ALL TESTS PASSED (100%)

---

## 1. Quadrature & Gradient Parity
- **Status**: ✅ PASSED
- **Height Max Error**: `4.20e-08`
- **GradU Max Error**: `2.48e-08`
- **GradV Max Error**: `4.88e-07`

## 2. Gradient Exactness (Analytical vs Numerical)
- **Status**: ✅ PASSED
- **Mean Error GradU**: `4.5057e-02`
- **Mean Error GradV**: `1.8967e-02`

## 3. Physics & Envelope Checks ($Fr = 0.50$)
- **Status**: ✅ PASSED
- **Centerline Transverse Wavelength**: `1.0027 λ` (Target: `1.0000 λ`)
- **Outer Cusp Decay Ratio**: `0.0129`

## 4. RGBA16F Quantization Parity
- **Status**: ✅ PASSED
- **Global Max Reconstructed Error**: `0.00840`
