<?php

header('Content-Type: text/html; charset=UTF-8');
header('Cross-Origin-Embedder-Policy: require-corp');
header('Cross-Origin-Opener-Policy: same-origin');

readfile('./index.html');
