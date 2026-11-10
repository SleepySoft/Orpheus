"""工作区单写者租约；接管与写请求串行，续租不等待长时间编译。"""

from __future__ import annotations

import asyncio
import secrets
import time
from collections.abc import Callable

from fastapi import FastAPI, Request
from fastapi.responses import JSONResponse
from pydantic import BaseModel

EDIT_HEADER = "X-Orpheus-Edit-Token"
LEASE_SECONDS = 30


class LeaseRequest(BaseModel):
    takeover: bool = False


class WorkspaceEditLease:
    def __init__(self, clock: Callable[[], float] = time.monotonic):
        self.clock = clock
        self.token: str | None = None
        self.deadline = 0.0
        self.gate = asyncio.Lock()

    def owns(self, token: str | None) -> bool:
        return bool(token and self.token == token and self.deadline > self.clock())

    def status(self) -> dict:
        remaining = max(0.0, self.deadline - self.clock())
        return {"occupied": bool(self.token and remaining), "expires_in": remaining,
                "lease_seconds": LEASE_SECONDS}

    def acquire(self, token: str | None, takeover: bool) -> dict | None:
        if self.status()["occupied"] and not self.owns(token) and not takeover:
            return None
        if not self.owns(token) or takeover:
            self.token = secrets.token_urlsafe(32)
        self.deadline = self.clock() + LEASE_SECONDS
        return {**self.status(), "token": self.token}


def install_edit_lease(app: FastAPI) -> None:
    lease = WorkspaceEditLease()
    app.state.edit_lease = lease
    prefix = "/api/editor"
    read_operations = {"/lesson/check", "/interfaces", "/rt/read", "/rt/read_bulk"}

    def refused() -> JSONResponse:
        return JSONResponse(status_code=409, content={
            "code": "edit_lease_lost",
            "detail": "当前页面没有编辑权或编辑权已过期，请接管并刷新后再编辑。",
        })

    @app.middleware("http")
    async def guard_writes(request: Request, call_next):
        path = request.url.path.rstrip("/")
        if (not path.startswith("/api/") or path.startswith(prefix + "/")
                or request.method in {"GET", "HEAD", "OPTIONS"}
                or (request.method == "POST"
                    and path.startswith("/api/projects/")
                    and any(path.endswith(suffix) for suffix in read_operations))):
            return await call_next(request)
        async with lease.gate:
            if not lease.owns(request.headers.get(EDIT_HEADER)):
                return refused()
            return await call_next(request)

    @app.get(prefix + "/lease")
    async def status():
        return lease.status()

    @app.post(prefix + "/acquire")
    async def acquire(body: LeaseRequest, request: Request):
        async with lease.gate:
            result = lease.acquire(request.headers.get(EDIT_HEADER), body.takeover)
            if result is None:
                return JSONResponse(status_code=409, content={
                    "code": "edit_lease_busy",
                    "detail": "工作区已在其他页面编辑，当前页面只读。",
                })
            return result

    @app.post(prefix + "/renew")
    async def renew(request: Request):
        if not lease.owns(request.headers.get(EDIT_HEADER)):
            return refused()
        lease.deadline = lease.clock() + LEASE_SECONDS
        return lease.status()

    @app.post(prefix + "/release")
    async def release(request: Request):
        async with lease.gate:
            if not lease.owns(request.headers.get(EDIT_HEADER)):
                return refused()
            lease.token = None
            lease.deadline = 0.0
            return lease.status()
