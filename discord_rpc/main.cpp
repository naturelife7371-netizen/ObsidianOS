// discord_rpc/main.cpp
// Discord Rich Presence client for ObsidianOS.
//
// REQUIREMENTS:
//   1. Discord desktop client must be running and logged in.
//   2. Discord Game SDK (discordpp.h + discord_game_sdk.dll/lib) extracted
//      next to this file. Download from:
//      https://discord.com/developers/docs/game-sdk
//   3. In the Discord Developer Portal for app 1573657423691124837:
//        Rich Presence -> Art Assets -> upload "icon.png", asset name "icon".
//   4. Rich Presence must be enabled for the application.
//
// BUILD (MinGW/g++ on Windows):
//   g++ -std=c++17 -O2 main.cpp -I./sdk/include -L./sdk/lib/x86_64 -ldiscord_game_sdk -o ObsidianRPC.exe
// BUILD (MSVC):
//   cl /std:c++17 /EHsc /O2 main.cpp /I sdk\include sdk\lib\x86_64\discord_game_sdk.dll.lib
//
// RUN:
//   ObsidianRPC.exe            (keep Discord open; Ctrl+C to stop)
//
// If Rich Presence does not appear, check:
//   - Discord is running and you are logged in.
//   - The asset key "icon" exists in Art Assets (exact name, no extension).
//   - The application ID matches 1573657423691124837.

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
    // SDK method name: SetLargeImage (string key). SetLargeImageText sets
    // the hover tooltip.
    activity.SetLargeImage("icon");
    activity.SetLargeImageText("ObsidianOS v0.2");

    // Optional: elapsed timer since start.
    activity.SetStartTimestamp(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

    discordpp::ActivityButton button1;
    button1.SetLabel("Get ObsidianOS");
    button1.SetUrl("https://github.com/naturelife7371-netizen/ObsidianOS");
    activity.AddButton(button1);

    client->UpdateRichPresence(activity, [](discordpp::ClientResult result) {
        if (!result.Successful()) {
            std::cerr << "Failed to update Rich Presence: "
                      << result.Error() << "\n";
        } else {
            std::cout << "Rich Presence updated successfully.\n";
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
        std::cerr << "Failed to create Discord client.\n"
                  << "Make sure Discord desktop is running and logged in.\n";
        return 1;
    }

    client->SetStatusChangedCallback([](discordpp::ClientResult result,
                                       discordpp::ClientStatus status) {
        if (result.Successful()) {
            std::cout << "Discord status changed: " << (int)status << "\n";
            if (status == discordpp::ClientStatus::Ready) {
                std::cout << "Client ready; updating presence.\n";
            }
        } else {
            std::cerr << "Status change failed.\n";
        }
    });

    // Initial presence update.
    UpdatePresence(client.get());

    std::cout << "ObsidianOS Rich Presence running (App ID "
              << APP_ID << "). Press Ctrl+C to stop.\n";

    // Main loop: pump callbacks so Discord can deliver events.
    while (g_running) {
        client->RunCallbacks();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        // Periodically re-push presence (every ~15s) in case it was cleared.
        static int tick = 0;
        if (++tick >= 75) {
            tick = 0;
            UpdatePresence(client.get());
        }
    }

    std::cout << "Shutting down Rich Presence.\n";
    return 0;
}
