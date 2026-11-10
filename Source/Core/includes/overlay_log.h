#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <utility>
#include <vector>
#include <format>
#include <atomic>
#include <Windows.h>

#include <imgui.h>

#define ADD_OVERLAY_INFO(...) Luma::OverlayLog::AddInfo(__VA_ARGS__)
#define ADD_OVERLAY_ERROR(...) Luma::OverlayLog::AddError(__VA_ARGS__)
#define ADD_OVERLAY_WARNING(...) Luma::OverlayLog::AddWarning(__VA_ARGS__)

// Prints messages on screen for a duration (as of now they are not stored on disk)
namespace Luma::OverlayLog
{
	using Clock = std::chrono::steady_clock;
	using TimePoint = Clock::time_point;

	// Constants:
	inline constexpr size_t g_max_messages = 128;
	inline constexpr float g_default_durability = 5.f;
	inline constexpr int g_dismiss_key = VK_END; // Unused by most games
	inline constexpr const char* g_dismiss_key_label = "END"; // Matches "g_dismiss_key"

	// Settings:
	inline bool show_messages = true;
	inline std::atomic<bool> g_messages_paused = false;

	enum class LogLevel : uint8_t
	{
		// Use for verbose events, or actual meaningful information
		Info,
		Warning,
		Error
	};

	enum class LogVisibility : uint8_t
	{
		All,
		// Development information that is either annoying or unnecessary for users
		DevOnly,
		// Messages meant for users that would be annoying or unnecessary for developers
		PublishingOnly
	};

	struct LogMessage
	{
		uint64_t id = 0;
		
		LogLevel level = LogLevel::Info;
		
		LogVisibility visibility = LogVisibility::All;

		// In seconds. <= 0 means infinite.
		float durability = g_default_durability;

		// If true, allow dismissing this message manually
		bool dismissible = false;

		// Pre-formatted by caller
		std::string text;

		TimePoint creation_time = Clock::now();
	};

	// Data:
	inline std::deque<LogMessage> g_messages;
	inline std::shared_mutex g_messages_mutex;
	inline std::atomic<uint64_t> g_next_message_id = 1;
	inline bool was_key_down = false;
	inline TimePoint g_pause_time = Clock::now();

	inline const char *GetLevelString(LogLevel level)
	{
		switch (level)
		{
		case LogLevel::Info:    return "Info";
		case LogLevel::Warning: return "Warning";
		case LogLevel::Error:   return "Error";
		default:                return "?";
		}
	}

	inline ImVec4 GetLevelColor(LogLevel level)
	{
		switch (level)
		{
		case LogLevel::Info:    return ImVec4(0.85f, 0.85f, 0.85f, 1.0f); // Light gray
		case LogLevel::Warning: return ImVec4(1.00f, 0.80f, 0.20f, 1.0f); // Orange
		case LogLevel::Error:   return ImVec4(1.00f, 0.35f, 0.35f, 1.0f); // Red
		default:                return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
		}
	}

	inline void SetMessagesVisibility(bool visibility)
	{
		show_messages = visibility;
	}

	inline uint64_t AddMessage(LogLevel level, std::string text, float durability = g_default_durability, bool dismissible = false, LogVisibility visibility = LogVisibility::All)
	{
		LogMessage message;
		message.id = g_next_message_id.fetch_add(1, std::memory_order_relaxed);
		message.level = level;
		message.durability = durability;
		message.visibility = visibility;
		message.dismissible = dismissible;
		message.text = std::move(text);
		message.creation_time = Clock::now();

		std::unique_lock lock(g_messages_mutex);

		// Optional cap so spam does not grow forever
		if (g_messages.size() >= g_max_messages)
		{
			g_messages.pop_front();
		}

		g_messages.emplace_back(std::move(message));
		return g_messages.back().id;
	}

	inline uint64_t AddInfo(std::string text, float durability = g_default_durability, bool dismissible = false, LogVisibility visibility = LogVisibility::All)
	{
		return AddMessage(LogLevel::Info, std::move(text), durability, dismissible, visibility);
	}
	inline uint64_t AddWarning(std::string text, float durability = g_default_durability, bool dismissible = false, LogVisibility visibility = LogVisibility::All)
	{
		return AddMessage(LogLevel::Warning, std::move(text), durability, dismissible, visibility);
	}
	inline uint64_t AddError(std::string text, float durability = g_default_durability, bool dismissible = false, LogVisibility visibility = LogVisibility::All)
	{
		return AddMessage(LogLevel::Error, std::move(text), durability, dismissible, visibility);
	}

	template <typename... Args>
	inline uint64_t AddInfo(std::format_string<Args...> format, Args&&... args)
	{
		return AddMessage(LogLevel::Info, std::format(format, std::forward<Args>(args)...));
	}

	template <typename... Args>
	inline uint64_t AddWarning(std::format_string<Args...> format, Args&&... args)
	{
		return AddMessage(LogLevel::Warning, std::format(format, std::forward<Args>(args)...));
	}

	template <typename... Args>
	inline uint64_t AddError(std::format_string<Args...> format, Args&&... args)
	{
		return AddMessage(LogLevel::Error, std::format(format, std::forward<Args>(args)...));
	}

	inline void RemoveMessage(uint64_t id)
	{
		std::unique_lock lock(g_messages_mutex);

		auto it = std::remove_if(
			g_messages.begin(),
			g_messages.end(),
			[id](const LogMessage &message)
			{
				return message.id == id;
			});

		g_messages.erase(it, g_messages.end());
	}

	inline void ClearMessages()
	{
		std::unique_lock lock(g_messages_mutex);
		g_messages.clear();
	}

	// Stops expiry time from elapsing.
	// Useful in case the game went through a log hitch during a loading screen, or in case it didn't create a swapchain for a while on boot etc
	inline void PauseMessages()
	{
		if (g_messages_paused.exchange(true, std::memory_order_acq_rel))
			return;

		g_pause_time = Clock::now();
	}

	inline void UnpauseMessages()
   {
		if (!g_messages_paused.exchange(false, std::memory_order_acq_rel))
			return;

		const TimePoint now = Clock::now();
		const auto paused_duration = now - g_pause_time;

		{
			std::unique_lock lock(g_messages_mutex);
			for (LogMessage& message : g_messages)
			{
				message.creation_time += paused_duration;
			}
		}
	}

	inline void Render()
	{
		std::vector<LogMessage> messages_snapshot;
		{
			std::shared_lock lock(g_messages_mutex);
			messages_snapshot.assign(g_messages.begin(), g_messages.end());
		}

		const bool is_key_down = (GetKeyState(g_dismiss_key) & 0x8000) != 0;
		bool key_pressed_event = is_key_down && !was_key_down;
		was_key_down = is_key_down;

		if (messages_snapshot.empty())
		{
			return;
		}

		const TimePoint now = Clock::now();
		std::vector<uint64_t> messages_to_remove;
		messages_to_remove.reserve(messages_snapshot.size());

		// Render straight into the current ReShade overlay / OSD callback.
		// No ImGui::Begin() / ImGui::End() here.
		for (size_t i = 0; i < messages_snapshot.size(); ++i)
		{
			const LogMessage& message = messages_snapshot[i];

#if DEVELOPMENT || TEST
			if (message.visibility == LogVisibility::PublishingOnly)
				continue;
#else // Publishing
			if (message.visibility == LogVisibility::DevOnly)
				continue;
#endif

			const bool is_infinite = message.durability <= 0.f;
			const float age = std::chrono::duration<float>(now - message.creation_time).count();
			const bool is_expired = !is_infinite && age >= message.durability;

			if (is_expired)
			{
				messages_to_remove.push_back(message.id);
				continue;
			}
			
			// Don't print them, but still count down
			if (!show_messages)
			{
				continue;
			}

			ImGui::PushID(static_cast<int>(message.id));

			ImGui::PushStyleColor(ImGuiCol_Text, GetLevelColor(message.level));
			ImGui::Text("%s: %s", GetLevelString(message.level), message.text.c_str());
			ImGui::PopStyleColor();

			// Only allow dismissing the newest message, if desired
			const bool is_last_message = (i + 1 == messages_snapshot.size());
			if (message.dismissible && is_last_message)
			{
				ImGui::SameLine();
				ImGui::Text(" [%s]", g_dismiss_key_label);
				if (key_pressed_event)
				{
					messages_to_remove.push_back(message.id);
					key_pressed_event = false;
				}
			}

			ImGui::PopID();
		}

		if (!messages_to_remove.empty())
		{
			std::unique_lock lock(g_messages_mutex);

			auto should_remove =
				[&messages_to_remove](const LogMessage &message)
				{
					return std::find(messages_to_remove.begin(), messages_to_remove.end(), message.id) != messages_to_remove.end();
				};

			auto it = std::remove_if(g_messages.begin(), g_messages.end(), should_remove);
			g_messages.erase(it, g_messages.end());
		}
	}
}