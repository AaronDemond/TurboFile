#pragma once

// Shared layout values for the side-by-side browser tab groups.
// Keeping them here avoids magic numbers in BrowserPane and MainWindow.

// The workspace never hosts more than two side-by-side tab groups.
inline constexpr int kMaxPaneGroups = 2;

// Smallest supported top-level window. This leaves enough room for the pinned
// sidebar, one browser pane, and the persistent bottom controls while still
// allowing compact desktop layouts. Two panes continue to enforce their own
// 200-pixel minima through the workspace splitter.
inline constexpr int kMainWindowMinimumWidth = 640;
inline constexpr int kMainWindowMinimumHeight = 420;

// Minimum width of each independent tab group. The splitter can compress a
// pane for a compact side-by-side layout, but it stops at 200 pixels rather
// than allowing the entire pane to collapse away.
inline constexpr int kPaneGroupMinimumWidth = 200;

// Hit target for the divider between the two tab groups. Matches the sidebar
// splitter so grab affordances feel the same across the window.
inline constexpr int kPaneSplitterHandleWidth = 8;
