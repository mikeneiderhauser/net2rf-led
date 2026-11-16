# HTTP API

The controller's JSON API, for scripts and integrations such as [Home Assistant](../guides/home-assistant.md).

It's documented in one place, [`openapi.yaml`](openapi.yaml) (OpenAPI 3.1): every endpoint with its fields, plus the
request rules, authentication, settings, receiver mode, coverage groups, controller discovery, board records and
firmware updates.

## On the controller

Every controller (firmware 0.3.2 and later) serves the spec and an explorer page for it:

| Address | What it is |
|---|---|
| `http://<controller>/api/docs` | **API explorer** (Swagger UI). *Try it out* sends real requests to this controller, with the admin login if one is set. Also linked from **System → Project → API**. |
| `http://<controller>/api/openapi.yaml` | The spec, matching the controller's firmware: load it into Postman, Insomnia or a client generator. |

> **The explorer page needs internet access in your browser.** It loads Swagger UI from unpkg.com; the controller
> itself needs no internet. Without it, the page says so and links the raw spec, which always works.

*Try it out* changes things for real: switching RF output off, starting a test, rebooting. Use it on a controller
that isn't running a show.

## Online

The [API explorer](https://mikeneiderhauser.github.io/net2rf-led/api/) published with each release shows the latest
release's spec (also needs internet). It can't send requests to a controller, because the controller doesn't answer
cross-site requests: use the controller's own page, or curl or a script. Locally:
`python3 -m http.server -d docs/reference`, then open `http://localhost:8000/openapi.html`.

## From a script

```bash
curl http://net2rf-3f2a.local/api/stats
curl -X POST -H "Content-Type: application/json" -d '{"enabled": false}' http://net2rf-3f2a.local/api/output
curl -u admin:yourpassword http://net2rf-3f2a.local/api/config      # once an admin password is set
```

Every `POST` needs `Content-Type: application/json`, and must address the controller by IP or its own name.
