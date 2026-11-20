// SPDX-License-Identifier: GPL-3.0-or-later
#include "PCH.h"

#include "Strings.h"

#include <atomic>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace
{
	constexpr auto             kTranslationFolder = L"Data/Interface/Translations";
	constexpr std::string_view kGameDefaultLanguage = "ENGLISH";  // when the game's sLanguage cannot be read

	// What Load hands out for the rest of the game: each text in the chosen language and its
	// pieces (Strings::Pieces).
	struct Published
	{
		Strings::Detail::Table                                           texts{};
		std::array<std::vector<Strings::Piece>, Strings::Detail::kCount> pieces;
		std::deque<std::string>                                          storage;    // the translations and the pieces' words; a deque keeps them in place
		char32_t                                                         probe = 0;  // Strings::Probe
	};

	// Contract: Load fills `g_published` once, then stores its address; Get and Pieces read it
	// only through `g_current`, so a reader sees it whole or not at all (then the English).
	Published                     g_published;
	std::atomic<const Published*> g_current{ nullptr };

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

	// The texts Livery_<language>.txt gives, or none when it is missing; logs what it read.
	std::vector<std::string> ReadTranslation(const std::string& a_language)
	{
		const auto    fileName = std::format("Livery_{}.txt", a_language);
		const auto    path = std::filesystem::current_path() / kTranslationFolder / fileName;
		std::ifstream in{ path, std::ios::binary };
		if (!in) {
			logger::info("No {}, so Livery stays in English", fileName);
			return {};
		}
		const std::string bytes{ std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
		auto              translation = Strings::Detail::Translate(bytes);
		for (const auto& problem : translation.problems) {
			logger::warn("{}: {}", fileName, problem);
		}
		logger::info("Read {} of {} texts from {}", translation.read, Strings::Detail::kCount, fileName);
		return std::move(translation.texts);
	}

	// Fills g_published from `a_texts` (empty or one per text; an empty one keeps the English)
	// and hands it out.
	void Publish(std::vector<std::string> a_texts)
	{
		using Strings::Detail::kCount;
		for (std::size_t i = 0; i < kCount; ++i) {
			const bool translated = i < a_texts.size() && !a_texts[i].empty();
			g_published.texts[i] = translated ? g_published.storage.emplace_back(std::move(a_texts[i])).c_str() : Strings::Detail::kEnglish[i];
			if (translated && g_published.probe == 0) {
				g_published.probe = Strings::Detail::FirstNonAscii(g_published.texts[i]);
			}
			for (auto& cut : Strings::Detail::CutAtValues(g_published.texts[i])) {
				const char* words = cut.value < 0 ? g_published.storage.emplace_back(std::move(cut.words)).c_str() : nullptr;
				g_published.pieces[i].push_back({ .words = words, .value = cut.value });
			}
		}
		g_current.store(&g_published, std::memory_order_release);
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
			Publish({});
			return;
		}
		Publish(ReadTranslation(language));
	}

	const char* Get(Text a_text)
	{
		const auto  index = static_cast<std::size_t>(a_text);
		const auto* published = g_current.load(std::memory_order_acquire);
		return published ? published->texts[index] : Detail::kEnglish[index];
	}

	char32_t Probe()
	{
		const auto* published = g_current.load(std::memory_order_acquire);
		return published ? published->probe : 0;
	}

	std::span<const Piece> Pieces(Text a_text)
	{
		const auto* published = g_current.load(std::memory_order_acquire);
		return published ? std::span<const Piece>{ published->pieces[static_cast<std::size_t>(a_text)] } : std::span<const Piece>{};
	}
}
