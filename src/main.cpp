#include <cstdlib>
#include <print>
#include <rtxui/rtxui.hpp>
#include <string_view>

#include "net/ssh_auth.hpp"
#include "ui/app.hpp"

#ifdef _WIN32
#include <windows.h>
#undef RGB
#endif

namespace {

constexpr std::string_view kUsage = R"(kenzo - log viewer for Toast Engine

usage: kenzo [options] [file.csv]

  --csv PATH          open a saved session instead of connecting
  --host IP[:PORT]    connect to a log server on the network (default port 12801)
  --reduced-motion    turn off animations (also KENZO_REDUCED_MOTION=1)
  -h, --help          show this help
  -v, --version       show the version

With no arguments kenzo looks for a log server on localhost and shows the menu.
Press ? inside kenzo for every key.
)";

}  // namespace

int main(int argc, char** argv) {
	// ssh-keygen runs kenzo as its askpass helper
	if (kenzo::net::isAskpassInvocation()) {
		return kenzo::net::askpassMain();
	}

	kenzo::ui::Options options;
	if (const char* env = std::getenv("KENZO_REDUCED_MOTION")) {
		options.reducedMotion = *env != '\0' && *env != '0';
	}

	for (int i = 1; i < argc; ++i) {
		const std::string_view arg = argv[i];
		const auto value = [&]() -> const char* {
			if (i + 1 >= argc) {
				std::println(stderr, "kenzo: {} needs a value", arg);
				std::exit(2);
			}
			return argv[++i];
		};
		if (arg == "-h" || arg == "--help") {
			std::print("{}", kUsage);
			return 0;
		}
		if (arg == "-v" || arg == "--version") {
			std::println("kenzo 2.1");
			return 0;
		}
		if (arg == "--reduced-motion") {
			options.reducedMotion = true;
		} else if (arg == "--csv") {
			options.csv = std::filesystem::path(value());
		} else if (arg == "--host") {
			kenzo::Target target;
			if (!kenzo::parseTarget(value(), target)) {
				std::println(stderr, "kenzo: invalid host '{}'", argv[i]);
				return 2;
			}
			options.host = target;
		} else if (!arg.starts_with('-')) {
			options.csv = std::filesystem::path(arg);
		} else {
			std::println(stderr, "kenzo: unknown option '{}'\n\n{}", arg, kUsage);
			return 2;
		}
	}

#ifdef _WIN32
	SetConsoleOutputCP(CP_UTF8);
#endif

	auto app = rtxui::Ref<kenzo::ui::App>::New(std::move(options));
	rtxui::Screen screen(app);
	screen.SetInline(false);
	screen.SetBackgroundColor(rtxui::Color::RGB(0x12, 0x11, 0x11));
	app->setQuit([&screen] { screen.Exit(); });
	screen.Loop();
	return 0;
}
