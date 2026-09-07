#pragma once

// Shared layout values for per-tab split panes.
// Keeping them here avoids magic numbers in BrowserPane and MainWindow.

// A tab never hosts more than two side-by-side explorers.
inline constexpr int kMaxPanesPerTab = 2;

// Hit target for the divider between two file panes. Matches the
// sidebar splitter so grab affordances feel the same across the window.
inline constexpr int kPaneSplitterHandleWidth = 8;
