"""单写者租约、过期与接管栅栏的 HTTP 回归。"""

import asyncio

import httpx
import pytest
from fastapi import FastAPI
from fastapi.testclient import TestClient

from orpheus_core.server.app import create_app
from orpheus_core.server.edit_lease import EDIT_HEADER, install_edit_lease


def acquire(client, takeover=False):
    response = client.post("/api/editor/acquire", json={"takeover": takeover})
    assert response.status_code == 200, response.text
    return {EDIT_HEADER: response.json()["token"]}


def test_takeover_fences_all_old_operations_and_reload(tmp_path):
    with TestClient(create_app(tmp_path)) as client:
        a = acquire(client)
        assert client.post("/api/projects", json={"name": "demo"}, headers=a).status_code == 201
        initial = client.get("/api/projects/demo").json()
        changed = {**initial, "metadata": {"name": "A"}}
        assert client.put("/api/projects/demo", json=changed, headers=a).status_code == 200
        assert client.post("/api/editor/acquire", json={}).status_code == 409
        b = acquire(client, takeover=True)
        assert b != a
        assert client.get("/api/projects/demo").json()["metadata"]["name"] == "A"
        for operation in ("renew", "release"):
            assert client.post(f"/api/editor/{operation}", headers=a).status_code == 409
        assert client.put("/api/projects/demo", json=initial, headers=a).status_code == 409
        assert client.post("/api/editor/renew", headers=b).status_code == 200
        assert client.get("/api/projects/demo").json()["metadata"]["name"] == "A"
        assert client.post("/api/editor/release", headers=b).status_code == 200
        assert not client.get("/api/editor/lease").json()["occupied"]


def test_expiration_without_cleanup_and_restart(tmp_path):
    app = create_app(tmp_path)
    now = [100.0]
    app.state.edit_lease.clock = lambda: now[0]
    with TestClient(app) as client:
        a = acquire(client)
        now[0] += 20
        assert client.post("/api/editor/renew", headers=a).status_code == 200
        now[0] += 29.999
        assert client.get("/api/editor/lease").json()["occupied"]
        now[0] = 150.0
        assert not client.get("/api/editor/lease").json()["occupied"]
        assert client.post("/api/editor/renew", headers=a).status_code == 409
        b = acquire(client)
        assert b != a
        assert client.post("/api/editor/release", headers=a).status_code == 409
    with TestClient(create_app(tmp_path)) as restarted:
        assert restarted.post("/api/editor/renew", headers=b).status_code == 409
        acquire(restarted)


@pytest.mark.parametrize("method,path", [
    ("POST", "/api/projects"), ("PUT", "/api/projects/demo"),
    ("DELETE", "/api/projects/demo"), ("POST", "/api/projects/demo/distill"),
    ("PUT", "/api/projects/demo/notes"), ("PUT", "/api/projects/demo/node_notes"),
    ("POST", "/api/projects/demo/uploads"), ("POST", "/api/projects/demo/compile"),
    ("POST", "/api/projects/demo/generate"), ("POST", "/api/projects/demo/run"),
    ("POST", "/api/projects/demo/rt/start"), ("POST", "/api/projects/demo/rt/param"),
    ("POST", "/api/components/rescan"), ("DELETE", "/api/components/shared"),
    ("POST", "/api/components/shared/promote"),
])
def test_no_token_cannot_write_any_surface(tmp_path, method, path):
    with TestClient(create_app(tmp_path)) as client:
        response = client.request(method, path, json={})
        assert response.status_code == 409
        assert response.json()["code"] == "edit_lease_lost"


def test_read_only_posts_do_not_need_edit_rights(tmp_path):
    with TestClient(create_app(tmp_path)) as client:
        acquire(client)
        for path in ("interfaces", "lesson/check", "rt/read", "rt/read_bulk"):
            response = client.post(f"/api/projects/missing/{path}", json={})
            assert response.status_code != 409


def test_takeover_waits_for_inflight_write_then_rejects_late_save():
    async def scenario():
        app = FastAPI()
        install_edit_lease(app)
        started = asyncio.Event()
        finish = asyncio.Event()
        writes = []

        @app.post("/api/write")
        async def write():
            started.set()
            await finish.wait()
            writes.append("saved")
            return {"saved": True}

        async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app),
                                     base_url="http://test") as client:
            first = (await client.post("/api/editor/acquire", json={})).json()
            a = {EDIT_HEADER: first["token"]}
            pending = asyncio.create_task(client.post("/api/write", headers=a))
            await started.wait()
            takeover = asyncio.create_task(
                client.post("/api/editor/acquire", json={"takeover": True}))
            await asyncio.sleep(0)
            assert not takeover.done()
            assert (await client.post("/api/editor/renew", headers=a)).status_code == 200
            finish.set()
            assert (await pending).status_code == 200
            assert (await takeover).status_code == 200
            assert (await client.post("/api/write", headers=a)).status_code == 409
            assert writes == ["saved"]
    asyncio.run(scenario())
