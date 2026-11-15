// SPDX-License-Identifier: GPL-3.0-or-later
#include "PCH.h"

#include "Strings.h"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace
{
	constexpr auto             kTranslationFolder = L"Data/Interface/Translations";
	constexpr std::string_view kGameDefaultLanguage = "ENGLISH";  // when the game's sLanguage cannot be read

	// The texts Get hands out: the English until Load publishes the translated table, which
	// then stays for the whole game. Load runs once, so `g_translated` and the strings it
	// points into are written before the store and never again.
	std::atomic<const Strings::Detail::Table*> g_current{ &Strings::Detail::kEnglish };
	Strings::Detail::Table                     g_translated{};
	std::vector<std::string>                   g_texts;

	// The game's language as its INI names it (sLanguage), as the game's own translation files
	// are chosen.
	// Contract: the game's INI settings are read by kDataLoaded.
	std::string GameLanguage()
	{
		auto*       settings = RE::INISettingCollection::GetSingleton();
		const auto* setting = settings ? settings->GetSetting("sLanguage:General") : nullptr;
		if (!setting || setting->GetType() != RE::Setting::Type::kString || !setting->data.s || !*setting->data.s) {
			return std::string{ kGameDefaultLanguage };
		}
		return setting->data.s;
	}
}

namespace Strings
{
	void Load(std::string_view a_language)
	{
		const auto language = a_language.empty() ? GameLanguage() : std::string{ a_language };
		logger::info("Language = {}{}", language, a_language.empty() ? " (Auto)" : "");
		if (!Detail::IsLanguageName(language)) {
			logger::warn("{} is not a language name (letters, digits and _ only), so Livery stays in English", language);
			return;
		}

		const auto    fileName = std::format("Livery_{}.txt", language);
		const auto    path = std::filesystem::current_path() / kTranslationFolder / fileName;
		std::ifstream in{ path, std::ios::binary };
		if (!in) {
			logger::info("No {}, so Livery stays in English", fileName);
			return;
		}
		const std::string bytes{ std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };

		auto translation = Detail::Translate(bytes);
		for (const auto& problem : translation.problems) {
			logger::warn("{}: {}", fileName, problem);
		}
		g_texts = std::move(translation.texts);
		for (std::size_t i = 0; i < Detail::kCount; ++i) {
			g_translated[i] = g_texts[i].empty() ? Detail::kEnglish[i] : g_texts[i].c_str();
		}
		g_current.store(&g_translated, std::memory_order_release);
		logger::info("Read {} of {} texts from {}", translation.read, Detail::kCount, fileName);
	}

	const char* Get(Text a_text)
	{
		return (*g_current.load(std::memory_order_acquire))[static_cast<std::size_t>(a_text)];
	}
}
