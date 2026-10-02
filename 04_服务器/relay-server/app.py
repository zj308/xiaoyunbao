import asyncio
import signal
import sys
import uuid
from pathlib import Path

import yaml


def load_bootstrap_config():
    config_path = Path(__file__).resolve().parent / "data" / ".config.yaml"
    if not config_path.exists():
        raise FileNotFoundError(f"找不到配置文件: {config_path}")
    with config_path.open("r", encoding="utf-8") as handle:
        return yaml.safe_load(handle) or {}


async def wait_for_exit():
    loop = asyncio.get_running_loop()
    stop_event = asyncio.Event()
    if sys.platform != "win32":
        for sig in (signal.SIGINT, signal.SIGTERM):
            loop.add_signal_handler(sig, stop_event.set)
        await stop_event.wait()
    else:
        await asyncio.Future()


async def run_standard_server():
    # 普通小智模式才加载 Opus、VAD、OTA 等完整服务依赖。
    from aioconsole import ainput
    from config.logger import setup_logging
    from config.settings import load_config
    from core.http_server import SimpleHttpServer
    from core.utils.gc_manager import get_gc_manager
    from core.utils.util import check_ffmpeg_installed, get_local_ip
    from core.websocket_server import WebSocketServer

    logger = setup_logging()
    check_ffmpeg_installed()
    config = await load_config()

    auth_key = config["server"].get("auth_key", "")
    if not auth_key or "你" in auth_key:
        auth_key = config.get("manager-api", {}).get("secret", "")
        if not auth_key or "你" in auth_key:
            auth_key = uuid.uuid4().hex
    config["server"]["auth_key"] = auth_key

    async def monitor_stdin():
        while True:
            await ainput()

    stdin_task = asyncio.create_task(monitor_stdin())
    gc_manager = get_gc_manager(interval_seconds=300)
    await gc_manager.start()
    ws_task = asyncio.create_task(WebSocketServer(config).start())
    ota_task = asyncio.create_task(SimpleHttpServer(config).start())
    port = int(config.get("server", {}).get("port", 8000))
    logger.bind(tag=__name__).info(
        "Websocket地址是\tws://{}:{}/xiaozhi/v1/", get_local_ip(), port
    )

    try:
        await wait_for_exit()
    finally:
        await gc_manager.stop()
        for task in (stdin_task, ws_task, ota_task):
            task.cancel()
        await asyncio.gather(stdin_task, ws_task, ota_task, return_exceptions=True)


async def main():
    bootstrap = load_bootstrap_config()
    relay = bootstrap.get("xiyunbao", {})
    if relay.get("enabled", False) and relay.get("only", False):
        from core.xiyunbao_app import run_xiyunbao_app

        await run_xiyunbao_app(bootstrap)
        return
    await run_standard_server()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("服务器已停止。")
