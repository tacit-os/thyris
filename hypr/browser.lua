-- Hyprland rules for ~/dev/browser's own windows (PLAN.md 3.17). Nothing loads this file by itself: it is linked
-- as ~/.config/hypr/custom/browser.lua and required after custom.rules in ~/.config/hypr/hyprland.lua
-- (safe_require("custom.browser")). The Qt WebEngine build's main window (class browser, opacity 0.85 0.77) has its rule
-- in the session's rules.lua.

-- The CEF build's main window is class browser-cef (src/main.cpp), which that rule misses: Hyprland matches a class
-- whole. The same glass here, scaled as the session scales it for a bright wallpaper (tacit_glass, hyprland.lua). The
-- private window (PLAN.md 3.14) is a main window of the same class: nothing about its colour or glass differs.
local glass = tacit_glass or function(o) return o end
hl.window_rule({ match = { class = "^(browser-cef)$" }, opacity = string.format("%.3f %.3f", glass(0.85), glass(0.77)) })

-- A sign-in popup in the notch (~/book4-edge/signin-notch-plan.md): its page alone, under the app_id browser-signin,
-- shown once the notch's sheet has settled, into the hole the shell leaves for it. It floats pinned (the notch is on
-- every workspace), opaque focused or not, undimmed, with no border, shadow, blur, rounding or animation: the hole's
-- rounded edge is the notch's, drawn over the page's square corners. The sheet is centred on its monitor, so the
-- page is too; 36 = the notch's 34 px header and a 2 px gap (island.topMargin 0). The shell moves it exactly where
-- the hole is if it lands elsewhere (another monitor).
hl.window_rule({ match = { class = "^(browser-signin)$" }, float = true })
hl.window_rule({ match = { class = "^(browser-signin)$" }, pin = true })
hl.window_rule({ match = { class = "^(browser-signin)$" }, move = "(monitor_w*0.5-window_w*0.5) 36" })
hl.window_rule({ match = { class = "^(browser-signin)$" }, no_anim = true })
hl.window_rule({ match = { class = "^(browser-signin)$" }, opacity = "1.0 override 1.0 override" })
hl.window_rule({ match = { class = "^(browser-signin)$" }, no_dim = true })
hl.window_rule({ match = { class = "^(browser-signin)$" }, no_blur = true })
hl.window_rule({ match = { class = "^(browser-signin)$" }, no_shadow = true })
hl.window_rule({ match = { class = "^(browser-signin)$" }, rounding = 0 })
hl.window_rule({ match = { class = "^(browser-signin)$" }, border_size = 0 })
