##############################################################################
# MIT License
#
# Copyright (c) 2025 Advanced Micro Devices, Inc. All Rights Reserved.
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.

##############################################################################

from typing import Any, Optional

import yaml
from textual import events
from textual.app import ComposeResult
from textual.containers import Container, VerticalScroll
from textual.widgets import Label

from rocprof_compute_tui.utils.tui_utils import Logger
from rocprof_compute_tui.widgets.decision_tree import TreeCanvas, TreeNode


class MemBWView(Container):
    """
    Memory Bandwidth Guided Analysis (Center Panel View)
    """

    DEFAULT_CSS = """
    MemBWView {
        layout: vertical;
    }

    #tree-container {
        height: 1fr;
        border: none;
        margin-top: 1;
    }

    .placeholder {
        color: $text-muted;
        padding: 0 1;
    }
    """

    def __init__(self, tree_yaml: Optional[str] = None) -> None:
        super().__init__(id="membw-view")
        self.logger = Logger()
        self.status_label: Optional[Label] = None
        self.tree_canvas: Optional[TreeCanvas] = None

        # Load decision tree from YAML
        yaml_path = (
            tree_yaml or "src/rocprof_compute_tui/utils/mem_bw_decision_tree.yaml"
        )
        try:
            with open(yaml_path) as f:
                tree_data = yaml.safe_load(f)
            self.root = TreeNode.from_dict(tree_data)
        except Exception as e:
            # Fallback to a trivial tree; also log the error
            self.logger.error(f"Failed to load {yaml_path}: {e}")
            self.root = TreeNode("Memory Bandwidth", "(no data)")

        self.logger.info("MemBWView initialized", update_ui=False)

    # ---- TUI lifecycle ----
    def compose(self) -> ComposeResult:
        """Compose a single scrollable container hosting the tree canvas."""
        with VerticalScroll(id="tree-container"):
            # If loading failed, we leave a placeholder. Otherwise mount the canvas.
            if hasattr(self, "root") and self.root:
                self.tree_canvas = TreeCanvas(self.root)
                yield self.tree_canvas
            else:
                yield Label("No tree data loaded.", classes="placeholder")

    # ---- Status line API (parity with KernelView) ----
    def update_view(self, message: str, log_level: str) -> None:
        if not self.status_label:
            self.status_label = Label(message, classes=log_level)
            self.mount(self.status_label)
        else:
            self.status_label.update(message)
            self.status_label.set_classes(log_level)

    # ---- Public API to replace the tree at runtime ----
    def set_tree_data(self, data: dict[str, Any]) -> None:
        self.root = TreeNode.from_dict(data)
        if self.tree_canvas is None:
            self.tree_canvas = TreeCanvas(self.root)
            self.query_one("#tree-container", VerticalScroll).mount(self.tree_canvas)
        else:
            self.tree_canvas.root = self.root
        self.tree_canvas.refresh(layout=True)

    # ---- Input forwarding (same behavior as the standalone app) ----
    async def on_key(self, event: events.Key) -> None:
        if not self.tree_canvas:
            return
        if event.key == "space":
            self.tree_canvas.toggle()
        elif event.key in ("w", "up"):
            self.tree_canvas.navigate("up")
        elif event.key in ("s", "down"):
            self.tree_canvas.navigate("down")
        elif event.key in ("a", "left"):
            self.tree_canvas.navigate("left")
        elif event.key in ("d", "right"):
            self.tree_canvas.navigate("right")

    async def on_mouse_down(self, event: events.MouseDown) -> None:
        if self.tree_canvas and hasattr(self.tree_canvas, "on_mouse_down"):
            await self.tree_canvas.on_mouse_down(event)
