"""已有业务回归使用真实编辑会话；租约回归直接使用原始 TestClient。"""

import threading

from fastapi.testclient import TestClient

from orpheus_core.server.edit_lease import EDIT_HEADER


class EditorClient(TestClient):
    def __enter__(self):
        super().__enter__()
        response = self.post("/api/editor/acquire", json={})
        response.raise_for_status()
        self.headers[EDIT_HEADER] = response.json()["token"]
        self._stop_renewing = threading.Event()
        self._renew_errors = []

        def renew():
            while not self._stop_renewing.wait(10):
                try:
                    self.post("/api/editor/renew").raise_for_status()
                except Exception as error:
                    self._renew_errors.append(error)
                    return

        self._renew_thread = threading.Thread(target=renew, daemon=True)
        self._renew_thread.start()
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        self._stop_renewing.set()
        self._renew_thread.join(timeout=10)
        try:
            self.post("/api/editor/release").raise_for_status()
            if self._renew_errors and exc_type is None:
                raise self._renew_errors[0]
        finally:
            return_value = super().__exit__(exc_type, exc_value, traceback)
        return return_value
