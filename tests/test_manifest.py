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
        self.assertTrue({"inventory", "map", "waypoints", "projects"}.issubset(seen), seen)

    def test_feature_tabs_have_separate_touch_areas(self):
        actions = self.manifest["actions"]
        for page in self.manifest["pages"]:
            tabs = [widget for widget in page["widgets"]
                    if widget.get("on_tap") in actions
                    and actions[widget["on_tap"]]["kind"] == "page"
                    and widget.get("rect", [0, 999])[1] < 100]
            self.assertEqual(len(tabs), 4, page["id"])
            targets = {actions[tab["on_tap"]]["page"] for tab in tabs}
            self.assertEqual(targets, {"inventory", "map", "waypoints", "projects"})
            for index, tab in enumerate(tabs):
                x, y, width, height = tab["rect"]
                self.assertGreater(width, 0)
                self.assertGreater(height, 0)
                self.assertLessEqual(x + width, self.manifest["canvas_w"])
                for other in tabs[index + 1:]:
                    ox, oy, ow, oh = other["rect"]
                    overlap = x < ox + ow and ox < x + width and y < oy + oh and oy < y + height
                    self.assertFalse(overlap, f"overlapping tabs on {page['id']}")


if __name__ == "__main__":
    unittest.main()
