#pragma once
#include "group_candidates.h"
namespace BodycamReference
{
struct OptionalMarkerPair
{
	int index = kNoMarker;
	std::optional<float> offset;
};
struct BlendMarkerNode
{
	float normalizedTime = 0;
	OptionalMarkerPair previous, next;
};
struct BlendMarkerSample
{
	std::string sequenceIdentity;
	double rateScale = 1, weight = 0;
	float time = 0, previousTime = 0;
	OptionalMarkerPair previous, next;
};
struct MarkerSequence
{
	float length, rateScale;
	std::array<std::string, 2> names;
	std::array<float, 2> times;
};
struct BlendMarkerEvent
{
	std::string kind;
	unsigned index;
};
struct BlendMarkerResult
{
	std::vector<BlendMarkerSample> samples;
	BlendMarkerNode node;
	CandidateContext context;
	unsigned selectedIndex;
	std::vector<BlendMarkerEvent> events;
};
BlendMarkerResult TickLocomotionMarkers(std::vector<BlendMarkerSample> samples, const std::map<std::string, MarkerSequence>& sequences,
	BlendMarkerNode node, CandidateContext context, double historyDelta);
}
