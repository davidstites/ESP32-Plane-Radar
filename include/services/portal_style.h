#pragma once

namespace services::portal {

/** Dark theme for the setup portal; no external assets (the AP has no internet). */
constexpr char kPortalHeadHtml[] = R"css(<style>
.c,body{text-align:center;font-family:system-ui,-apple-system,sans-serif}
body{background:#0a0f0c;color:#d7f5e3;margin:0;padding:12px}
.wrap{text-align:left;max-width:460px;min-width:260px}
h1,h2,h3{color:#34d27b;font-weight:600}
a{color:#34d27b}a:hover{color:#7bf0ad}
input,select{background:#121b16;color:#d7f5e3;border:1px solid #2a3d32;padding:12px;font-size:1rem}
input:focus,select:focus{outline:2px solid #34d27b;border-color:#34d27b}
input[type=checkbox]{accent-color:#34d27b;width:1.25rem;height:1.25rem;vertical-align:middle;margin-right:8px}
button,input[type=submit]{background:#34d27b;color:#04110a;font-weight:700;line-height:3rem;border-radius:.6rem}
button.D{background:#e5534b;color:#fff}
.msg{background:#121b16;color:#d7f5e3;border-color:#2a3d32}
dt,td{color:#d7f5e3}
details.adv{margin:12px 0;padding:4px 12px;background:#121b16;border:1px solid #2a3d32;border-radius:.6rem}
details.adv summary{cursor:pointer;padding:10px 0;color:#8aa196;font-weight:600}
</style>)css";

static_assert(sizeof(kPortalHeadHtml) <= 3072, "portal CSS over its 3 KB budget");

}  // namespace services::portal
