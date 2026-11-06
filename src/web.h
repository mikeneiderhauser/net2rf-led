#pragma once

// HTTP server: single-page UI at "/" plus a JSON API under /api (see docs/reference/openapi.yaml).
namespace web {

void begin();
void loop();

}  // namespace web
