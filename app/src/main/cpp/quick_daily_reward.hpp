#pragma once
#include "runtime.hpp"

namespace ms::quick_daily_reward {
// CN 7.2 dump and the matching ELF. The handbook entry is the actual reward
// button's callback. AddDialogOption observes the game's nearby NPC list.
inline constexpr char buildId[] = "6d85e3141bcd6b508f3e853bab508cc407278eac";
inline constexpr Ptr handbookReward = 0xe074acc;      // BNHCLKOHEFI.CEOFJIJACFL
inline constexpr Ptr addDialogOption = 0xe343c28;     // MonoInteePanel.AddDialogOption
inline constexpr Ptr setPreSelectCheck = 0x11c18888;  // MonoSelectItem.SetPreSelectCheck
inline constexpr Ptr rewardCheckCall = 0x101e3c48;    // SetupSelectItemByConfig, TAKE_DAILY_TASK_REWARD
inline constexpr Ptr dialogOptionAt = 0xe343830;      // MonoInteePanel.GetDialogOptionAt
inline constexpr Ptr pickupText = 0xfe6f638;          // MonoPickupListItem.get_text
inline constexpr Ptr activeAndEnabled = 0x9dd52d0;    // Behaviour.get_isActiveAndEnabled
inline constexpr Ptr interactable = 0xc618820;        // Selectable.IsInteractable
inline constexpr Ptr pressButton = 0x105f243c;        // Button.Press
inline constexpr Ptr taskReturn = 0xe3c0320;          // MonoAdventureHandbookPage.get_taskReturnButton
inline constexpr Ptr containerButton = 0xcd0f2c4;     // MonoUIContainer.GetButton

class Flow {
    Engine engine{0};
    Ptr panel = 0;
    uint32_t panelHandle = 0;
    Ptr reward = 0;
    Ptr rewardCheck = 0;
    uint32_t rewardHandle = 0;
    uint64_t deadline = 0;
    enum class Phase { Idle, ReturnToHud, WaitForReward };
    Phase phase = Phase::Idle;

    template<class Result, class... Args>
    Result call(Ptr offset, Args... args) {
        return reinterpret_cast<Result(*)(Args..., Ptr)>(engine.base + offset)(args..., Ptr(0));
    }
    static bool alive(Ptr component) {
        return component && load<Ptr>(component + 0x10);
    }
    uint32_t root(Ptr object) {
        return engine.call<uint32_t>(abi::pin, object, 2, Ptr(0));
    }
    void clearPanel() {
        if (panelHandle) engine.call<void>(abi::unpin, panelHandle);
        panel = 0;
        panelHandle = 0;
    }
    void clearReward() {
        if (rewardHandle) engine.call<void>(abi::unpin, rewardHandle);
        reward = 0;
        rewardCheck = 0;
        rewardHandle = 0;
    }
    void cancel() {
        phase = Phase::Idle;
        clearReward();
    }
    bool canPress(Ptr button) {
        return alive(button) && call<bool>(activeAndEnabled, button) &&
               call<bool>(interactable, button);
    }
    Ptr katheryneButton() {
        if (!alive(panel)) return 0;
        const Ptr dialogList = load<Ptr>(panel + 0x20);
        if (!alive(dialogList)) return 0;
        const Ptr items = load<Ptr>(dialogList + 0x40); // MonoReusableList's live rows
        if (!items) return 0;
        const int count = load<int>(items + 0x18);
        if (count <= 0 || count > 64) return 0;
        Ptr button = 0;
        for (int i = 0; i < count; ++i) {
            const Ptr row = call<Ptr>(dialogOptionAt, panel, i);
            if (!alive(row) || !alive(load<Ptr>(row + 0x38))) continue;
            const Ptr text = call<Ptr>(pickupText, row);
            constexpr char16_t name[] = u"凯瑟琳";
            if (!text || load<int>(text + 0x10) != 3 ||
                std::memcmp(reinterpret_cast<const void*>(text + 0x14), name, 3 * sizeof(char16_t))) continue;
            const Ptr candidate = load<Ptr>(row + 0x30);
            if (!alive(candidate)) continue;
            if (button) return 0;
            button = candidate;
        }
        return button;
    }

public:
    void bind(Ptr base) { engine.base = base; }

    void observePanel(Ptr value) {
        if (panel == value || !alive(value)) return;
        clearPanel();
        panelHandle = root(value);
        if (panelHandle) panel = value;
    }

    bool handbookClicked(Ptr controller, uint64_t now) {
        if (phase != Phase::Idle) {
            cancel();
            return false;
        }
        if (!controller || !katheryneButton()) return false;
        const Ptr page = load<Ptr>(controller + 0x310);
        if (!alive(page)) return false;
        const Ptr back = call<Ptr>(taskReturn, page);
        if (!alive(back)) return false;
        const Ptr button = call<Ptr>(containerButton, back);
        if (!canPress(button)) return false;
        // The handbook covers the HUD. Use its regular return button first,
        // then wait for the live interaction to become clickable.
        phase = Phase::ReturnToHud;
        deadline = now + 1500;
        call<void>(pressButton, button);
        return true;
    }

    void observeReward(Ptr row, Ptr check, Ptr caller, uint64_t now) {
        if (phase != Phase::WaitForReward || now >= deadline ||
            caller != engine.base + rewardCheckCall + 4 || !alive(row) || !check) return;
        if (reward == row && rewardCheck == check) return;
        if (reward) {
            cancel();
            return;
        }
        rewardHandle = root(row);
        if (!rewardHandle) return;
        reward = row;
        rewardCheck = check;
    }

    void tick(bool enabled, uint64_t now) {
        if (panel && !alive(panel)) clearPanel();
        if (phase == Phase::Idle) return;
        if (!enabled || now >= deadline) {
            cancel();
            return;
        }
        if (phase == Phase::ReturnToHud) {
            const Ptr button = katheryneButton();
            if (!canPress(button)) return;
            phase = Phase::WaitForReward;
            deadline = now + 5000;
            call<void>(pressButton, button);
            return;
        }
        if (!reward) return;
        if (!alive(reward) || load<Ptr>(reward + 0x58) != rewardCheck) {
            cancel();
            return;
        }
        const Ptr button = load<Ptr>(reward + 0x28);
        if (!canPress(button)) return;
        // The button invokes OnSelectItem, including the game's preselect check.
        phase = Phase::Idle;
        call<void>(pressButton, button);
        clearReward();
    }
};
}
