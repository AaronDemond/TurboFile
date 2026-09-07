#pragma once

// Shared layout and animation values for the pinned-directory sidebar.
// Keeping them in one header avoids magic numbers across the sidebar files.

// Height of one pin row, and of the animated drop gap that opens between rows.
inline constexpr int kPinRowHeight = 32;
inline constexpr int kPlaceholderEndHeight = 32;

// Duration of placeholder open/close and unpin collapse animations.
inline constexpr int kPinAnimationMs = 140;

// First-run expanded splitter width. The expanded pane has no min/max cap.
inline constexpr int kSidebarHintWidth = 180;

// Width of the leftover chevron strip when the sidebar is collapsed.
inline constexpr int kSidebarCollapsedWidth = 24;

// Hit target for the sidebar/tab splitter. Desktop styles often draw a
// 1px line; this makes the handle wide enough to grab comfortably.
inline constexpr int kSidebarSplitterHandleWidth = 8;

// Custom MIME type used when dragging an existing pin to reorder it.
// External filesystem drags still use file:// URLs.
inline constexpr char kPinnedDirectoryMime[] =
    "application/x-turbofile-pinned-directory";
