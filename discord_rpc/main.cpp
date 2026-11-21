// discord_rpc/main.cpp
// Discord Rich Presence client for ObsidianOS.
//
// Build (Windows, with Discord Game SDK extracted next to this file):
//   g++ -std=c++17 main.cpp -I./discord_game_sdk/include -L./discord_game_sdk/lib/x86_64 -ldiscord_game_sdk -o ObsidianRPC.exe
//
// Or with MSVC:
//   cl /std:c++17 /EHsc main.cpp /I discord_game_sdk\include discord_game_sdk\lib\x86_64\discord_game_sdk.dll.lib
//
// Place discord_game_sdk.dll next to the resulting .exe (or in PATH).

#include "discordpp.h"
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>
#include <atomic>

static std::atomic<bool> g_running{true};

void UpdatePresence(discordpp::Client* client)
{
    discordpp::Activity activity;
    activity.SetType(discordpp::ActivityTypes::Playing);
    activity.SetDetails("Running ObsidianOS");
    activity.SetState("CLI & RTL8139 Server Node");

    // Large image: asset key "icon" (uploaded as icon.png in the
    // Discord Developer Portal -> Rich Presence -> Art Assets).
    activity.SetLargeImage("icon");
    activity.SetLargeImageText("ObsidianOS");

    discordpp::ActivityButton button1;
    button1.SetLabel("Get ObsidianOS");
    button1.SetUrl("https://github.com/naturelife7371-netizen/ObsidianOS");
    activity.AddButton(button1);

    client->UpdateRichPresence(activity, [](discordpp::ClientResult result) {
        if (!result.Successful()) {
            std::cerr << "Failed to update Rich Presence\n";
        }
    });
}

static void on_signal(int) { g_running = false; }

int main()
{
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    const discordpp::ClientId APP_ID = 1573657423691124837ULL;

    auto client = discordpp::Client::Create(APP_ID);
    if (!client) {
        std::cerr << "Failed to create Discord client. Is Discord running?\n";
        return 1;
    }

    client->SetStatusChangedCallback([](discordpp::ClientResult result,
                                       discordpp::ClientStatus status) {
        if (result.Successful()) {
            std::cout << "Discord status: " << (int)status << "\n";
        } else {
            std::cerr << "Status change failed\n";
        }
    });

    UpdatePresence(client.get());

    std::cout << "ObsidianOS Rich Presence running. Press Ctrl+C to stop.\n";
    while (g_running) {
        client->RunCallbacks();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    std::cout << "Shutting down Rich Presence.\n";
    return 0;
}
