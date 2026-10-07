"""Check that feature controls remain reachable when their manifests are combined."""

import json
import unittest
from pathlib import Path


MANIFEST = Path(__file__).resolve().parents[1] / "package/dualscreen/manifest.json"


def widgets(value):
    if isinstance(value, dict):
        yield value
        for child in value.values():
            yield from widgets(child)
    elif isinstance(value, list):
        for child in value:
            yield from widgets(child)


class ManifestTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.manifest = json.loads(MANIFEST.read_text())

    def test_every_control_and_page_action_has_a_target(self):
        actions = self.manifest["actions"]
        pages = [page["id"] for page in self.manifest["pages"]]
        self.assertEqual(len(pages), len(set(pages)), "duplicate page ID")
        for name, action in actions.items():
            with self.subTest(action=name):
                if action["kind"] == "page":
                    self.assertIn(action["page"], pages)
        for widget in widgets(self.manifest["pages"]):
            for name, action in widget.items():
                if name.startswith("on_") and isinstance(action, str):
                    with self.subTest(control=name, action=action):
                        self.assertIn(action, actions)

    def test_features_are_reachable_from_inventory(self):
        actions = self.manifest["actions"]
        pages = {page["id"]: page for page in self.manifest["pages"]}
        seen, pending = set(), ["inventory"]
        while pending:
            page = pending.pop()
            if page in seen:
                continue
            seen.add(page)
            for widget in widgets(pages[page]):
                for name, target in widget.items():
                    if name.startswith("on_") and isinstance(target, str) and target in actions:
                        action = actions[target]
                        if action["kind"] == "page":
                            pending.append(action["page"])
        self.assertTrue({"inventory", "map", "waypoints", "projects", "effects"}.issubset(seen), seen)

    def test_feature_tabs_have_separate_touch_areas(self):
        actions = self.manifest["actions"]
        for page in self.manifest["pages"]:
            tabs = [widget for widget in page["widgets"]
                    if widget.get("on_tap") in actions
                    and actions[widget["on_tap"]]["kind"] == "page"
                    and widget.get("rect", [0, 999])[1] < 100]
            self.assertEqual(len(tabs), 5, page["id"])
            targets = {actions[tab["on_tap"]]["page"] for tab in tabs}
            self.assertEqual(targets, {"inventory", "map", "waypoints", "projects", "effects"})
            for index, tab in enumerate(tabs):
                x, y, width, height = tab["rect"]
                self.assertGreater(width, 0)
                self.assertGreater(height, 0)
                self.assertLessEqual(x + width, self.manifest["canvas_w"])
                for other in tabs[index + 1:]:
                    ox, oy, ow, oh = other["rect"]
                    overlap = x < ox + ow and ox < x + width and y < oy + oh and oy < y + height
                    self.assertFalse(overlap, f"overlapping tabs on {page['id']}")

    def test_effect_rows_and_paging_fit_on_screen(self):
        page = next(page for page in self.manifest["pages"] if page["id"] == "effects")
        for widget in page["widgets"]:
            if widget.get("repeat"):
                self.assertEqual(widget["repeat"], 8)
                self.assertEqual(widget.get("need_bind"), "effects.row{i}.visible")
                x, y, width, height = widget["rect"]
                self.assertLess(y + height + 7 * widget["repeat_dy"], 986)
        for action, binding in [("effects_previous", "effects.can_prev"),
                                ("effects_next", "effects.can_next")]:
            button = next(w for w in page["widgets"] if w.get("on_tap") == action)
            self.assertEqual(button["need_bind"], binding)

    def test_item_touch_covers_inventory_hotbar_and_equipment(self):
        page = next(page for page in self.manifest["pages"] if page["id"] == "inventory")
        slots = {}
        for widget in page["widgets"]:
            if widget.get("on_tap") != "item_inspect":
                continue
            start = int(widget["payload"].removeprefix("{i+").removesuffix("}"))
            x, y, width, height = widget["rect"]
            columns = widget["repeat_cols"]
            for i in range(widget["repeat"]):
                slot = start + i
                self.assertNotIn(slot, slots)
                rx = x + (i % columns) * widget["repeat_dx"]
                ry = y + (i // columns) * widget.get("repeat_row_dy", 0)
                self.assertLessEqual(rx + width, self.manifest["canvas_w"])
                self.assertLessEqual(ry + height, self.manifest["canvas_h"])
                slots[slot] = (rx, ry, width, height)
        self.assertEqual(set(slots), set(range(40)))
        self.assertEqual(self.manifest["actions"]["item_inspect"]["argument"], "$payload")
        drawer = [widget for widget in page["widgets"] if widget.get("need_bind") == "detail.open"]
        self.assertTrue(any(widget.get("on_tap") == "item_close" for widget in drawer))
        for slot, (x, y, width, height) in slots.items():
            if slot < 36:
                self.assertLessEqual(y + height, 786, "drawer must leave inventory slots reachable")

    def test_day_clock_is_visible_between_tabs_and_content(self):
        for page in self.manifest["pages"]:
            clock = [w for w in page["widgets"]
                     if w.get("bind_text", "").startswith("clock.")]
            self.assertEqual({w["bind_text"] for w in clock}, {"clock.time", "clock.sunset"})
            self.assertEqual(len(clock), 3, page["id"])
            self.assertEqual({w.get("need_bind") for w in clock
                              if w["bind_text"] == "clock.sunset"},
                             {"clock.soon", "!clock.soon"})
            for w in clock:
                x, y, _, _ = w["rect"]
                self.assertGreaterEqual(y, 64)
                self.assertLessEqual(y + 8 * w["text_scale"], 100)
                self.assertLessEqual(x, self.manifest["canvas_w"])
            for w in widgets(page):
                if any(k.startswith("on_") for k in w) and "rect" in w:
                    if w["rect"][1] < 100:
                        self.assertLessEqual(w["rect"][1] + w["rect"][3], 64)
                    else:
                        self.assertGreaterEqual(w["rect"][1], 100)


if __name__ == "__main__":
    unittest.main()
