// Just enough of Unreal's Core for HLSnellSolver.cpp to compile and run without
// the engine. Mirrors the UE 5 semantics the solver uses (FVector is double).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#define HANGINGLAKE_API
#define KINDA_SMALL_NUMBER (1.e-4f)

using int32 = std::int32_t;

template <typename T>
struct TNumericLimits
{
	static constexpr T Max() { return std::numeric_limits<T>::max(); }
};

struct FMath
{
	template <typename T> static T Sqrt(T V) { return std::sqrt(V); }
	template <typename T> static T Abs(T V) { return std::abs(V); }
	template <typename T> static T Clamp(T V, T Lo, T Hi) { return std::min(std::max(V, Lo), Hi); }
	template <typename T> static T Max(T A, T B) { return std::max(A, B); }
	template <typename T> static T Min(T A, T B) { return std::min(A, B); }
	static bool IsNearlyZero(double V, double Tol = 1.e-8) { return std::abs(V) <= Tol; }
};

struct FVector
{
	double X = 0.0, Y = 0.0, Z = 0.0;

	FVector() = default;
	FVector(double InX, double InY, double InZ) : X(InX), Y(InY), Z(InZ) {}

	static const FVector UpVector;
	static const FVector ZeroVector;

	FVector operator+(const FVector& O) const { return {X + O.X, Y + O.Y, Z + O.Z}; }
	FVector operator-(const FVector& O) const { return {X - O.X, Y - O.Y, Z - O.Z}; }
	FVector operator-() const { return {-X, -Y, -Z}; }
	FVector operator*(double S) const { return {X * S, Y * S, Z * S}; }
	FVector operator/(double S) const { return {X / S, Y / S, Z / S}; }
	friend FVector operator*(double S, const FVector& V) { return V * S; }

	double Size() const { return std::sqrt(X * X + Y * Y + Z * Z); }
	double Size2D() const { return std::sqrt(X * X + Y * Y); }
	FVector GetSafeNormal() const
	{
		const double L = Size();
		return L > 1.e-8 ? *this / L : FVector();
	}

	static double DotProduct(const FVector& A, const FVector& B) { return A.X * B.X + A.Y * B.Y + A.Z * B.Z; }
	static FVector CrossProduct(const FVector& A, const FVector& B)
	{
		return {A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X};
	}
	static double Dist(const FVector& A, const FVector& B) { return (A - B).Size(); }
	static double Dist2D(const FVector& A, const FVector& B) { return (A - B).Size2D(); }
};

inline const FVector FVector::UpVector(0.0, 0.0, 1.0);
inline const FVector FVector::ZeroVector(0.0, 0.0, 0.0);

template <typename T>
struct TArray
{
	std::vector<T> Items;
	void Reset() { Items.clear(); }
	void Add(const T& V) { Items.push_back(V); }
	int32 Num() const { return static_cast<int32>(Items.size()); }
	bool IsEmpty() const { return Items.empty(); }
	T& operator[](int32 I) { return Items[I]; }
	const T& operator[](int32 I) const { return Items[I]; }
	const T& Last() const { return Items.back(); }
};
