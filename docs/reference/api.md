# HTTP API

The controller's JSON API, for scripts and integrations such as [Home Assistant](../guides/home-assistant.md).

It's documented in one place, [`openapi.yaml`](openapi.yaml) (OpenAPI 3.1): every endpoint with its fields, plus the
request rules, authentication, settings, receiver mode, coverage groups, controller discovery, board records and
firmware updates.

- **Read it** in the [API explorer](https://mikeneiderhauser.github.io/net2rf-led/api/), published with each
  release, or locally: `python3 -m http.server -d docs/reference`, then open `http://localhost:8000/openapi.html`.
- **Use it** in Postman, Insomnia or a client generator by loading the file.

The explorer can't send requests to a controller (the controller doesn't answer cross-site requests). Use curl or a
script:

```bash
curl http://net2rf-3f2a.local/api/stats
curl -X POST -H "Content-Type: application/json" -d '{"enabled": false}' http://net2rf-3f2a.local/api/output
curl -u admin:yourpassword http://net2rf-3f2a.local/api/config      # once an admin password is set
```

Every `POST` needs `Content-Type: application/json`, and must address the controller by IP or its own name.
