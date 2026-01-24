import email
import http
import re
from pathlib import Path
import mimetypes
import gzip

from websockets.datastructures import Headers
from websockets.asyncio.server import ServerConnection, Request, Response

NO_CACHE_PATTERN = r'.*/auth.json'
CACHE_TIME_SEC = 300

def get_brotli():
    try:
        import brotli
        return brotli
    except:
        return None


brotli = get_brotli()

def compress_br(input_path: Path, output_path: Path):
    chunk_size = 1024 * 1024 
    compressor = brotli.Compressor(quality=11)
    with input_path.open("rb") as fin, output_path.open("wb") as fout:
        while chunk := fin.read(chunk_size):
            fout.write(compressor.process(chunk))
        fout.write(compressor.finish())

def compress_gzip(input_path: Path, output_path: Path):
    chunk_size = 1024 * 1024 
    with input_path.open("rb") as fin, gzip.open(output_path, "wb", compresslevel=9) as fout:
        while chunk := fin.read(chunk_size):
            fout.write(chunk)

    
root = Path(__file__).parent.parent / "client"
cmp_root = root / "._compressed_cache"


def get_compressed_file(filename: str) -> Path | None:
    method = "br" if brotli else "gzip"
    path = root / filename
    time = round(path.stat().st_mtime)
    cmp_path = cmp_root / f'{filename}.cmp.{time}.{method}'
    old_path = cmp_root / f'{filename}.cmp.*.*'
    if cmp_path.exists():
        return (cmp_path, method)
    cmp_dir = cmp_path.parent
    if not cmp_dir.exists():
        cmp_dir.mkdir(parents=True, exist_ok=True)
    for old_file in old_path.parent.glob(old_path.name):
        try:
            old_file.unlink(missing_ok=True)
        except:
            pass
    try:
        if brotli:
            compress_br(path, cmp_path)
        else:
            compress_gzip(path, cmp_path)
        return (cmp_path, method)
    except:
        try:
            cmp_path.unlink(missing_ok=True)
        except:
            pass
        return (path, None)



def serve_file(filename: str, connection: ServerConnection, request: Request) -> Response:
    try:
        # File path resolution and security check
        parts = filename.split('/')
        parts = map(lambda x: re.sub(r'[^a-z0-9._-]', '', x, flags=re.IGNORECASE), parts)
        parts = filter(lambda x: x != '' and not x.startswith('.'), parts)
        path = root / '/'.join(parts)
        if not path.exists():
            return connection.respond(http.HTTPStatus.NOT_FOUND, 'Not Found')
        elif not path.is_file():
            return connection.respond(http.HTTPStatus.FORBIDDEN, 'Forbidden')

        # Basic headers
        headers = [
            ("Date", email.utils.formatdate(usegmt=True)),
            ("Connection", "close"),
        ]

        if not re.match(NO_CACHE_PATTERN, filename):
            # Handle caching with ETag
            headers.append(("Cache-Control", f"public, max-age={CACHE_TIME_SEC}"))
            stat = path.stat()
            file_etag = f'"{stat.st_mtime_ns:x}-{stat.st_size:x}"'
            user_etag = request.headers.get("If-None-Match", "")
            headers.append(("ETag", file_etag))
            if user_etag == file_etag:
                status = http.HTTPStatus(http.HTTPStatus.NOT_MODIFIED)
                return Response(status.value, status.phrase, Headers(headers), b'')
        else:
            # Handle no-cache files
            headers.append(("Cache-Control", "no-store"))
            headers.append(("Pragma", "no-cache"))
            headers.append(("Expires", "0"))

        # Determine MIME type
        mime_type, _ = mimetypes.guess_type(path.name)
        if mime_type is None:
            mime_type = "application/octet-stream"
        if mime_type.startswith("text/"):
            mime_type = f"{mime_type}; charset=utf-8"
        headers.append(("Content-Type", mime_type))
        
        # Check if compression is supported and serve compressed file if possible
        accept_encoding = request.headers.get("Accept-Encoding", "")
        known_encodings_count = len(set(filter(lambda x: x == 'br' or x == 'gzip', map(lambda x: x.strip().lower(), accept_encoding.split(',')))))
        send_path, method = (path, None)
        if known_encodings_count == 2:
            send_path, method = get_compressed_file(filename)
        if method:
            headers.append(("Content-Encoding", method))

        # Read and send file content
        body = send_path.read_bytes()
        headers.append(("Content-Length", str(len(body))))
        status = http.HTTPStatus(http.HTTPStatus.OK)
        return Response(status.value, status.phrase, Headers(headers), body)

    except Exception as e:
        print(e)
        return connection.respond(http.HTTPStatus.INTERNAL_SERVER_ERROR, 'Internal Server Error')
