#include "group_preparation.h"
#include "selection_sort.h"
#include <algorithm>
#include <stdexcept>
namespace BodycamReference
{
GroupPreparation PrepareLocomotionGroup(std::vector<SyncRecord> records, const std::optional<PreparedGroupSnapshot>& previous,
	const std::map<std::string, MarkerState>& nodes, std::array<std::string, 2> syncNames)
{
	if (syncNames[0].empty() || syncNames[1].empty() || syncNames[0] == syncNames[1])
		throw std::invalid_argument("Sync marker names must be two distinct nonempty names");
	const auto known = [&](const std::vector<std::string>& names)
	{
		if (names.size() > 2 || (names.size() == 2 && names[0] == names[1]))
			return false;
		return std::all_of(names.begin(), names.end(), [&](const std::string& n) { return n == syncNames[0] || n == syncNames[1]; });
	};
	for (unsigned i = 0; i < records.size(); ++i)
	{
		auto& r = records[i];
		if (!known(r.markerNames))
			throw std::invalid_argument("Player marker names must be a subset of the sync marker names");
		r.registrationIndex = i;
		r.markerEligible = false;
	}
	SortByDescendingWeight(records, [](const SyncRecord& r) { return r.weight; });
	GroupPreparation out;
	out.nodeStates = nodes;
	const auto reset = [&](const SyncRecord& r, const char* reason)
	{
		auto& node = out.nodeStates.at(r.nodeMarkerIdentity);
		node.previous.index = kNoMarker;
		node.next.index = kNoMarker;
		out.resets.push_back({ r.registrationIndex, r.nodeMarkerIdentity, reason });
	};
	auto names = records[0].markerNames;
	if (!names.empty())
	{
		records[0].markerEligible = true;
		for (unsigned i = 1; i < records.size(); ++i)
		{
			auto& r = records[i];
			if (names.empty() || r.markerNames.empty())
				continue;
			r.markerEligible = true;
			for (int n = int(names.size()) - 1; n >= 0; --n)
				if (std::find(r.markerNames.begin(), r.markerNames.end(), names[n]) == r.markerNames.end())
				{
					names[n] = names.back();
					names.pop_back();
				}
		}
		if (previous)
			for (const auto& r : records)
			{
				const auto old = std::find_if(previous->records.begin(), previous->records.end(),
					[&](const SyncRecord& o) { return o.nodeMarkerIdentity == r.nodeMarkerIdentity; });
				if (old == previous->records.end() || old->assetIdentity != r.assetIdentity)
					reset(r, "new-node-or-asset");
			}
	}
	if (names.empty())
		for (auto& r : records)
		{
			r.markerEligible = false;
			reset(r, "marker-mode-disabled");
		}
	else
	{
		std::vector<std::string> ordered;
		for (const auto& name : syncNames)
			if (std::find(names.begin(), names.end(), name) != names.end())
				ordered.push_back(name);
		names = std::move(ordered);
		if (!previous || previous->validNames != names)
			for (const auto& r : records)
				reset(r, "group-marker-names-changed");
	}
	out.records = std::move(records);
	out.validNames = std::move(names);
	out.markerEnabled = !out.validNames.empty();
	return out;
}
}
