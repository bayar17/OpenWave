"""Unprivileged adapter from Waydroid 1.6.3 package metadata to appd.

This module never invokes privileged Waydroid operations and intentionally does
not launch Android activities. It is designed to run inside the existing
Waydroid manager/session process and is dependency-injected for host tests.
"""
from __future__ import annotations

from dataclasses import dataclass
import hashlib
import re
from typing import Any, Callable, Iterable, Protocol

MAX_APPS = 256
MAX_CATEGORIES = 32
MAX_NAME = 256
MAX_FIELD = 512
LAUNCHER_CATEGORY = "android.intent.category.LAUNCHER"
PACKAGE_RE = re.compile(r"^[A-Za-z][A-Za-z0-9_]*(?:\.[A-Za-z][A-Za-z0-9_]*)+$")
COMPONENT_RE = re.compile(r"^\.?[A-Za-z][A-Za-z0-9_.$]*$")


class Platform(Protocol):
    def getAppsInfo(self) -> list[dict[str, Any]]: ...
    def getAppInfo(self, package: str) -> dict[str, Any] | None: ...


class CatalogSink(Protocol):
    def sync_android_catalog(self, user_id: int, applications: list[dict[str, Any]]) -> None: ...
    def set_android_runtime_state(self, user_id: int, online: bool) -> None: ...


@dataclass(frozen=True)
class AndroidApplication:
    app_id: str
    user_id: int
    package: str
    component: str
    name: str
    icon: str
    categories: tuple[str, ...]
    launch_capability: str = "disabled-until-security-and-surface-gates"
    launchable: bool = False

    def to_wire(self) -> dict[str, Any]:
        return {
            "appId": self.app_id,
            "androidUserId": self.user_id,
            "packageName": self.package,
            "componentName": self.component,
            "name": self.name,
            "icon": self.icon,
            "categories": list(self.categories),
            "launchCapability": self.launch_capability,
            "launchable": self.launchable,
        }


def _string(raw: Any, field: str, limit: int) -> str:
    if not isinstance(raw, str) or not raw or len(raw) > limit or "\x00" in raw:
        raise ValueError(f"invalid Android application {field}")
    return raw


def application_id(user_id: int, package: str, component: str) -> str:
    """Stable, reserved appd namespace; component hash avoids lossy escaping."""
    digest = hashlib.sha256(component.encode("utf-8")).hexdigest()[:24]
    return f"org.openwave.android.u{user_id}.{package}.{digest}"


def normalize_application(raw: dict[str, Any], user_id: int) -> AndroidApplication | None:
    if not isinstance(user_id, int) or isinstance(user_id, bool) or not 0 <= user_id <= 2**31 - 1:
        raise ValueError("invalid Android user ID")
    if not isinstance(raw, dict):
        raise ValueError("invalid Android application record")
    if "androidUserId" in raw and raw["androidUserId"] != user_id:
        raise ValueError("Android catalog record belongs to another user")
    package = _string(raw.get("packageName"), "package name", MAX_FIELD)
    if not PACKAGE_RE.fullmatch(package):
        raise ValueError("invalid Android package name")
    component_package = _string(raw.get("componentPackageName", package), "component package", MAX_FIELD)
    component_class = _string(raw.get("componentClassName"), "launcher component", MAX_FIELD)
    if component_package != package or not COMPONENT_RE.fullmatch(component_class):
        raise ValueError("invalid Android launcher component")
    if component_class.startswith("."):
        component_class = package + component_class
    elif "." not in component_class:
        component_class = package + "." + component_class
    component = f"{package}/{component_class}"
    name = _string(raw.get("name"), "name", MAX_NAME).strip()
    if not name:
        raise ValueError("empty Android application name")
    raw_categories = raw.get("categories", [])
    if not isinstance(raw_categories, (list, tuple)) or len(raw_categories) > MAX_CATEGORIES:
        raise ValueError("invalid Android categories")
    categories = tuple(dict.fromkeys(_string(value, "category", MAX_FIELD) for value in raw_categories))
    if LAUNCHER_CATEGORY not in categories:
        return None
    # Waydroid's pinned IPlatform exposes no icon URI or safe file identity.
    # A URI is metadata only; no filesystem path supplied by Android is trusted.
    supplied_icon = raw.get("icon")
    if supplied_icon is not None and supplied_icon != f"android-app://{package}":
        raise ValueError("invalid Android icon reference")
    icon = f"android-app://{package}"
    return AndroidApplication(application_id(user_id, package, component), user_id,
                              package, component, name, icon, categories)


def normalize_catalog(records: Iterable[dict[str, Any]], user_id: int) -> list[dict[str, Any]]:
    result: dict[str, AndroidApplication] = {}
    count = 0
    for raw in records:
        count += 1
        if count > MAX_APPS:
            raise ValueError("Android application catalog exceeds the 256 app limit")
        app = normalize_application(raw, user_id)
        if app is None:
            continue
        previous = result.get(app.app_id)
        if previous is not None and previous != app:
            raise ValueError("conflicting duplicate Android launcher component")
        result[app.app_id] = app
    return [result[key].to_wire() for key in sorted(result)]


class AndroidAppProvider:
    """Synchronize one Android user's launcher catalog through an injected sink."""

    def __init__(self, sink: CatalogSink, platform_factory: Callable[[], Platform | None]):
        self._sink = sink
        self._platform_factory = platform_factory
        self._active_user: int | None = None
        self._cache: dict[int, list[dict[str, Any]]] = {}
        self._online: dict[int, bool] = {}

    @property
    def cached_users(self) -> tuple[int, ...]:
        return tuple(sorted(self._cache))

    def user_unlocked(self, user_id: int) -> bool:
        self._active_user = user_id
        platform = self._platform_factory()
        if platform is None:
            self.runtime_state(user_id, False)
            return False
        try:
            catalog = normalize_catalog(platform.getAppsInfo(), user_id)
            self._sink.sync_android_catalog(user_id, catalog)
        except Exception:
            self.runtime_state(user_id, False)
            return False
        self._cache[user_id] = catalog
        self.runtime_state(user_id, True)
        return True

    def package_changed(self, package: str, mode: int, user_id: int | None = None) -> bool:
        del package, mode  # IPlatform's list is the source of truth for every event.
        selected = self._active_user if user_id is None else user_id
        return self.user_unlocked(selected) if selected is not None else False

    def runtime_state(self, user_id: int, online: bool) -> None:
        self._online[user_id] = online
        self._sink.set_android_runtime_state(user_id, online)

    def session_stopped(self) -> None:
        if self._active_user is not None:
            self.runtime_state(self._active_user, False)
        self._active_user = None


class DbusCatalogSink:
    """Small system-bus client for appd's manager-authenticated catalog methods."""

    def __init__(self, bus: Any | None = None):
        if bus is None:
            import dbus
            bus = dbus.SystemBus()
        self._dbus = __import__("dbus")
        self._object = bus.get_object("org.openwave.Applications", "/org/openwave/Applications")
        self._interface = self._dbus.Interface(self._object, "org.openwave.Applications1")

    def sync_android_catalog(self, user_id: int, applications: list[dict[str, Any]]) -> None:
        dbus = self._dbus
        records = []
        for app in applications:
            record = dict(app)
            record["androidUserId"] = dbus.UInt32(record["androidUserId"])
            record["categories"] = dbus.Array([dbus.String(item) for item in record["categories"]], signature="s")
            record["launchable"] = dbus.Boolean(record["launchable"])
            records.append(dbus.Dictionary(record, signature="sv"))
        self._interface.SyncAndroidCatalog(dbus.UInt32(user_id), dbus.Array(records, signature="a{sv}"), timeout=3)

    def set_android_runtime_state(self, user_id: int, online: bool) -> None:
        self._interface.SetAndroidRuntimeState(self._dbus.UInt32(user_id), self._dbus.Boolean(online), timeout=2)
