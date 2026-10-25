#pragma once

#include <rtxui/rtxui.hpp>
#include <string>
#include <vector>

namespace kenzo::ui {

struct GridRun {
	std::string cls, text, ink;
	int x = 0, y = 0, width = 0;
	bool operator==(const GridRun&) const = default;
};
inline void Fields(const GridRun& v, rtxui::FieldSink& out) {
	out("cls", v.cls);
	out("text", v.text);
	out("ink", v.ink);
	out("x", v.x);
	out("y", v.y);
	out("width", v.width);
}

struct MenuSessionRow {
	std::string cls, icon, when, info, badge;
	bool operator==(const MenuSessionRow&) const = default;
};
inline void Fields(const MenuSessionRow& v, rtxui::FieldSink& out) {
	out("cls", v.cls);
	out("icon", v.icon);
	out("when", v.when);
	out("info", v.info);
	out("badge", v.badge);
}

struct LogRow {
	std::string cls, ptr, bar, tag, t0, t1, t2, sk0, sk1, sk2, f0, f1, f2, m0, m1, m2, m3, m4, m5, m6, more;
	bool operator==(const LogRow&) const = default;
};
inline void Fields(const LogRow& v, rtxui::FieldSink& out) {
	out("cls", v.cls);
	out("ptr", v.ptr);
	out("bar", v.bar);
	out("tag", v.tag);
	out("t0", v.t0);
	out("t1", v.t1);
	out("t2", v.t2);
	out("sk0", v.sk0);
	out("sk1", v.sk1);
	out("sk2", v.sk2);
	out("f0", v.f0);
	out("f1", v.f1);
	out("f2", v.f2);
	out("m0", v.m0);
	out("m1", v.m1);
	out("m2", v.m2);
	out("m3", v.m3);
	out("m4", v.m4);
	out("m5", v.m5);
	out("m6", v.m6);
	out("more", v.more);
}

struct BarCell {
	std::string cls;
	bool operator==(const BarCell&) const = default;
};
inline void Fields(const BarCell& v, rtxui::FieldSink& out) {
	out("cls", v.cls);
}

struct SevRow {
	std::string cls, chk_cls, chk, color, name, count, key;
	bool operator==(const SevRow&) const = default;
};
inline void Fields(const SevRow& v, rtxui::FieldSink& out) {
	out("cls", v.cls);
	out("chk_cls", v.chk_cls);
	out("chk", v.chk);
	out("color", v.color);
	out("name", v.name);
	out("count", v.count);
	out("key", v.key);
}

struct SinkRow {
	std::string cls, chk_cls, chk, name, count;
	std::string k0, k1, k2, k3, l0, l1, l2, l3, i0, i1, i2, i3;
	bool operator==(const SinkRow&) const = default;
};
inline void Fields(const SinkRow& v, rtxui::FieldSink& out) {
	out("cls", v.cls);
	out("chk_cls", v.chk_cls);
	out("chk", v.chk);
	out("name", v.name);
	out("count", v.count);
	out("k0", v.k0);
	out("k1", v.k1);
	out("k2", v.k2);
	out("k3", v.k3);
	out("l0", v.l0);
	out("l1", v.l1);
	out("l2", v.l2);
	out("l3", v.l3);
	out("i0", v.i0);
	out("i1", v.i1);
	out("i2", v.i2);
	out("i3", v.i3);
}

struct SessionRow {
	std::string cls, when, name, badge, info;
	bool operator==(const SessionRow&) const = default;
};
inline void Fields(const SessionRow& v, rtxui::FieldSink& out) {
	out("cls", v.cls);
	out("when", v.when);
	out("name", v.name);
	out("badge", v.badge);
	out("info", v.info);
}

struct RemoteRow {
	std::string cls, host, user, when, duration, size;
	bool operator==(const RemoteRow&) const = default;
};
inline void Fields(const RemoteRow& v, rtxui::FieldSink& out) {
	out("cls", v.cls);
	out("host", v.host);
	out("user", v.user);
	out("when", v.when);
	out("duration", v.duration);
	out("size", v.size);
}

struct HelpRow {
	std::string keys, text, cls;
	bool operator==(const HelpRow&) const = default;
};
inline void Fields(const HelpRow& v, rtxui::FieldSink& out) {
	out("keys", v.keys);
	out("text", v.text);
	out("cls", v.cls);
}

struct CtxItem {
	std::string cls, label, key, arrow;
	bool operator==(const CtxItem&) const = default;
};
inline void Fields(const CtxItem& v, rtxui::FieldSink& out) {
	out("cls", v.cls);
	out("label", v.label);
	out("key", v.key);
	out("arrow", v.arrow);
}

struct TextLine {
	std::string text, cls;
	bool operator==(const TextLine&) const = default;
};
inline void Fields(const TextLine& v, rtxui::FieldSink& out) {
	out("text", v.text);
	out("cls", v.cls);
}

}  // namespace kenzo::ui
