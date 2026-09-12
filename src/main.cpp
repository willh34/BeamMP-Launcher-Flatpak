/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Gui.h"
#include "Http.h"
#include "Logger.h"
#include "Network/network.hpp"
#include "Options.h"
#include "Security/Init.h"
#include "Startup.h"
#include "Utils.h"
#include <curl/curl.h>
#include <iostream>
#include <thread>

Options options;

[[noreturn]] void flush() {
    while (true) {
        std::cout.flush();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

int main(int argc, const char** argv) {
#if defined(_WIN32)
    system("cls");
#endif

#ifdef DEBUG
    std::thread th(flush);
    th.detach();
#endif

    curl_global_init(CURL_GLOBAL_ALL);
    GetEP(Utils::ToWString(std::string(argv[0])).c_str());
    InitLog();
    ConfigInit();
    InitOptions(argc, argv, options);

    // All launcher stages are collected here so the same code path is used
    // whether running in GUI mode (worker thread) or headless (direct call).
    auto launcher_work = [&]() {
        try {
            Gui::SetStatus("Checking for launcher updates...");
            InitLauncher();

            // In headless mode the terminal is the only UI — remind the user.
            if (!Gui::IsEnabled())
                info("IMPORTANT: You MUST keep this window open to play BeamMP!");

            Gui::SetStatus("Finding BeamNG.drive installation...");
            try {
                LegitimacyCheck();
            } catch (std::exception& e) {
                error("Failure in LegitimacyCheck: " + std::string(e.what()));
                throw;
            }

            Gui::SetStatus("Starting network proxy...");
            try {
                HTTP::StartProxy();
            } catch (const std::exception& e) {
                error(std::string("Failed to start HTTP proxy: Some in-game functions "
                                  "may not work. Error: ") + e.what());
            }

            Gui::SetStatus("Downloading BeamMP mod...");
            PreGame(GetGameDir());

            Gui::SetStatus("Launching BeamNG.drive...");
            InitGame(GetGameDir());

            // CoreNetwork() blocks for the lifetime of the game session.
            Gui::SetStatus("BeamMP active \xe2\x80\x94 keep this window open to play!");
            Gui::ShowRunningIndicator();
            CoreNetwork();

            Gui::Shutdown();
        } catch (const std::exception& e) {
            const std::string msg = e.what();
            error("Exception in main(): " + msg);
            info("Closing in 5 seconds");
            info("If this keeps happening, contact us on either: "
                 "Forum: https://forum.beammp.com, Discord: https://discord.gg/beammp");
            Gui::ShowError("BeamMP Launcher Error", msg);
            if (!Gui::IsEnabled())
                std::this_thread::sleep_for(std::chrono::seconds(5));
            Gui::Shutdown();
        }
    };

#if defined(__linux__)
    if (!options.no_gui) {
        // GTK runs on the main thread; launcher_work runs on a background thread.
        Gui::Run(argc, argv, launcher_work);
    } else {
        system("clear");
        launcher_work();
    }
#else
    launcher_work();
#endif
}
