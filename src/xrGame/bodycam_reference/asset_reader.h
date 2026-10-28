#pragma once
#include "constants.h"
#include "pertrack_pose.h"
#include <cctype>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
namespace BodycamReference
{
// Commit of the prototype the asset packages were exported from. Every package header carries it.
constexpr std::string_view kAssetSourceRevision = "2b72576a7c8b4c0d6c2fb28f4f7483373ccb66ad";
constexpr unsigned kMaxAssetStringLength = 4096;

// Bone and track names are matched case-insensitively.
inline std::string LowerAscii(std::string text)
{
	for (auto& c : text)
		c = char(std::tolower(static_cast<unsigned char>(c)));
	return text;
}

// Little-endian reader for the exported asset packages.
class ReferenceAssetReader
{
public:
	explicit ReferenceAssetReader(const std::vector<std::uint8_t>& bytes) : bytes_(bytes) {}
	// A package starts with an 8-byte tag, a format version and the source revision.
	void ExpectHeader(std::string_view tag, std::uint32_t version)
	{
		const auto p = Take(8);
		if (std::string_view(reinterpret_cast<const char*>(p), 8) != tag)
			throw std::invalid_argument("Asset package is not " + std::string(tag));
		if (U32() != version)
			throw std::invalid_argument("Unsupported " + std::string(tag) + " package version");
		if (String() != kAssetSourceRevision)
			throw std::invalid_argument(std::string(tag) + " package was exported from another source revision");
	}
	std::uint32_t U32()
	{
		const auto p = Take(4);
		return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
	}
	int I32()
	{
		const auto bits = U32();
		std::int32_t value;
		std::memcpy(&value, &bits, 4);
		return value;
	}
	float F32()
	{
		const auto bits = U32();
		float value;
		std::memcpy(&value, &bits, 4);
		if (!std::isfinite(value))
			throw std::invalid_argument("Nonfinite asset float");
		return value;
	}
	double F64()
	{
		const auto low = U32(), high = U32();
		const std::uint64_t bits = low | (std::uint64_t(high) << 32);
		double value;
		std::memcpy(&value, &bits, 8);
		if (!std::isfinite(value))
			throw std::invalid_argument("Nonfinite asset double");
		return value;
	}
	unsigned Count(unsigned maximum)
	{
		const auto n = U32();
		if (n > maximum)
			throw std::invalid_argument("Asset count exceeds its limit");
		return n;
	}
	bool Bool() { return Count(1) != 0; }
	std::string String()
	{
		const auto n = Count(kMaxAssetStringLength);
		const auto p = Take(n);
		return std::string(reinterpret_cast<const char*>(p), n);
	}
	std::vector<std::uint8_t> Bytes(unsigned count)
	{
		const auto p = Take(count);
		return { p, p + count };
	}
	std::vector<LocalPose> LocalPoses(std::size_t count)
	{
		std::vector<LocalPose> out(count);
		for (auto& pose : out)
		{
			for (auto& v : pose.position)
				v = F64();
			for (auto& v : pose.rotation)
				v = F64();
			for (auto& v : pose.scale)
				v = F64();
		}
		return out;
	}
	std::vector<SourceBoneTransform> Pose()
	{
		std::vector<SourceBoneTransform> out(Count(kSkeletonBoneCount));
		for (auto& t : out)
		{
			for (auto& v : t.p)
				v = F64();
			for (auto& v : t.q)
				v = F64();
			for (auto& v : t.s)
				v = F64();
		}
		return out;
	}
	bool Done() const { return position_ == bytes_.size(); }

private:
	const std::uint8_t* Take(std::size_t count)
	{
		if (count > bytes_.size() - position_)
			throw std::invalid_argument("Truncated asset package");
		const auto p = bytes_.data() + position_;
		position_ += count;
		return p;
	}
	const std::vector<std::uint8_t>& bytes_;
	std::size_t position_ = 0;
};
}
