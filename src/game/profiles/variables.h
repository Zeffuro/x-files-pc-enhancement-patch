#pragma once
#include "generated.h"
#include <array>

namespace native_game {
struct VariableSlot {
    const wchar_t* registration;
    std::array<std::uint32_t, 4> rvas;
    const wchar_t* label = nullptr;

    std::uint32_t rva(const Profile& profile) const {
        if (profile.application == profile_cd_10012.application) {
            return rvas[0];
        }
        if (profile.application == profile_cd_10019.application) {
            return rvas[1];
        }
        if (profile.application == profile_dvd_20000.application) {
            return rvas[2];
        }
        if (profile.application == profile_cd_10020.application) {
            return rvas[3];
        }
        return 0;
    }
};

inline constexpr VariableSlot registered_variables[] = {
    {L"RegUberVars [1]", {0x2b9cf0, 0x2bccf8, 0x2c4880, 0x2bdd18}},
    {L"RegUberVars [2]", {0x2b9d28, 0x2bcd30, 0x2c48b8, 0x2bdd50}},
    {L"RegUberVars [3]", {0x2b9d5c, 0x2bcd64, 0x2c48ec, 0x2bdd84}},
    {L"RegNode2Vars [1]", {0x2b838c, 0x2bb38c, 0x2bf514, 0x2bc3ac}},
    {L"RegNode2Vars [2]", {0x2b841c, 0x2bb41c, 0x2bf5a4, 0x2bc43c}},
    {L"RegNode2Vars [3]", {0x2b8378, 0x2bb378, 0x2bf500, 0x2bc398}},
    {L"RegNode3Vars [1]", {0x2b8380, 0x2bb380, 0x2bf508, 0x2bc3a0}},
    {L"RegNode3Vars [2]", {0x2b8384, 0x2bb384, 0x2bf50c, 0x2bc3a4}},
    {L"RegNode3Vars [3]", {0x2b8364, 0x2bb364, 0x2bf4ec, 0x2bc384}},
    {L"RegNode3Vars [4]", {0x2b837c, 0x2bb37c, 0x2bf504, 0x2bc39c}},
    {L"RegNode4Vars [1]", {0x2b8420, 0x2bb420, 0x2bf5a8, 0x2bc440}},
    {L"RegNode4Vars [2]", {0x2b845c, 0x2bb45c, 0x2bf5e4, 0x2bc47c}},
    {L"RegNode5Vars [1]", {0x2b842c, 0x2bb42c, 0x2bf5b4, 0x2bc44c}},
    {L"RegNode5Vars [2]", {0x2b83a4, 0x2bb3a4, 0x2bf52c, 0x2bc3c4}},
    {L"RegNode5Vars [3]", {0x2b83cc, 0x2bb3cc, 0x2bf554, 0x2bc3ec}},
    {L"RegNode6Vars [1]", {0x2b83ac, 0x2bb3ac, 0x2bf534, 0x2bc3cc}},
    {L"RegNode6Vars [2]", {0x2b8428, 0x2bb428, 0x2bf5b0, 0x2bc448}},
    {L"RegLink2Vars [1]", {0x2b83e4, 0x2bb3e4, 0x2bf56c, 0x2bc404}},
    {L"RegLink2Vars [2]", {0x2b83e8, 0x2bb3e8, 0x2bf570, 0x2bc408}},
    {L"RegLink3Vars [1]", {0x2b83b0, 0x2bb3b0, 0x2bf538, 0x2bc3d0}},
    {L"RegLink3Vars [2]", {0x2b8388, 0x2bb388, 0x2bf510, 0x2bc3a8}},
    {L"RegCurrInvVar [1]",
     {0x2b897c, 0x2bb97c, 0x2bf764, 0x2bc99c},
     L"Current inventory selection"},
};
}
