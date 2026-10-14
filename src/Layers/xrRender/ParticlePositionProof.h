#pragma once
#include <string>
#include <sstream>
#include <regex>

// Narrow bytecode admission: four direct DP4s of POSITION by one native matrix.
// Unsupported compiler forms simply retain the old path; no shader is rewritten.
inline bool particle_position_dp4(const char* code, unsigned input, unsigned output,
    unsigned buffer, unsigned first)
{
    std::istringstream lines(code);
    std::string line;
    unsigned written = 0;
    const std::regex dp4(R"(^\s*dp4\s+o([0-9]+)\.([xyzw]),\s*(\S+),\s*(\S+)\s*$)");
    const std::regex position("\\bo" + std::to_string(output) + "(?:\\.|\\b)");
    while (std::getline(lines, line))
    {
        line = line.substr(0, line.find("//"));
        const auto begin = line.find_first_not_of(" \t\r");
        if (begin == std::string::npos) continue;
        line.erase(0, begin);
        if (line.find("if_") == 0 || line.find("loop") == 0 || line.find("switch") == 0 ||
            line.find("call") == 0 || line.find("retc") == 0 || line.find("label") == 0)
            return false;
        if (line.find("ret") == 0 && written != 15) return false;
        if (line.find("dcl_") == 0 || !std::regex_search(line, position)) continue;
        std::smatch m;
        if (!std::regex_match(line, m, dp4)) return false;
        const unsigned axis = unsigned(std::string("xyzw").find(m[2].str()));
        const std::string vertex = "v" + std::to_string(input) + ".xyzw";
        const std::string matrix = "cb" + std::to_string(buffer) + "[" + std::to_string(first + axis) + "].xyzw";
        // FXC normally emits the constant operand first; DP4 is commutative.
        if (std::stoul(m[1]) != output || (written & (1u << axis)) ||
            !((m[3] == vertex && m[4] == matrix) || (m[3] == matrix && m[4] == vertex))) return false;
        written |= 1u << axis;
    }
    return written == 15;
}
