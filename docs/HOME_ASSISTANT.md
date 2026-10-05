# Home Assistant

The controller has a plain JSON API, so Home Assistant can read its stats and control it with the built-in
[RESTful](https://www.home-assistant.io/integrations/rest/) and
[RESTful Command](https://www.home-assistant.io/integrations/rest_command/) integrations. No custom component needed.

Replace `net2rf-3f2a.local` with your controller's hostname or IP. A static IP (or a DHCP reservation) is more
reliable than `.local` names from inside Home Assistant OS.

## Sensors and status (`configuration.yaml`)

`/api/stats` is readable without the admin password.

```yaml
rest:
  - resource: http://net2rf-3f2a.local/api/stats
    scan_interval: 10
    sensor:
      - name: "Bracelets input rate"
        value_template: "{{ value_json.input.fps }}"
        unit_of_measurement: "fps"
        state_class: measurement
      - name: "Bracelets input packets"
        value_template: "{{ value_json.input.packets }}"
        state_class: total_increasing
      - name: "Bracelets RF updates"
        value_template: "{{ value_json.output.updates }}"
        state_class: total_increasing
      - name: "Bracelets RF airtime"
        value_template: "{{ value_json.output.airtime_pct }}"
        unit_of_measurement: "%"
        state_class: measurement
      - name: "Bracelets RF errors"
        value_template: "{{ value_json.output.errors }}"
        state_class: total_increasing
      - name: "Bracelets radio"
        value_template: "{{ value_json.radio }}"          # ready / initializing / not_detected
      - name: "Bracelets zone 1 colour"
        value_template: "#{{ value_json.zones[0].rgb }}"
      - name: "Bracelets firmware"
        value_template: "{{ value_json.firmware }}"
      - name: "Bracelets uptime"
        value_template: "{{ value_json.uptime_s }}"
        unit_of_measurement: "s"
        device_class: duration
    binary_sensor:
      - name: "Bracelets receiving"
        value_template: "{{ value_json.input.seen and not value_json.input.timed_out and value_json.input.age_ms < 5000 }}"
      - name: "Bracelets RF output"
        value_template: "{{ value_json.output_enabled }}"
      - name: "Bracelets firmware update available"
        value_template: "{{ value_json.update.available }}"
        device_class: update
```

## Controls

These endpoints need the admin password if you set one. Store it in `secrets.yaml`.

```yaml
rest_command:
  bracelets_output:
    url: http://net2rf-3f2a.local/api/output
    method: POST
    content_type: "application/json"
    payload: '{"enabled": {{ enabled }}}'
    username: admin                      # omit both lines if no admin password is set
    password: !secret bracelets_admin_password
  bracelets_color:
    url: http://net2rf-3f2a.local/api/test
    method: POST
    content_type: "application/json"
    payload: '{"mode": "{{ mode }}", "rgb": "{{ rgb }}"}'   # mode: off / solid / cycle
    username: admin
    password: !secret bracelets_admin_password

  bracelets_all_off:
    url: http://net2rf-3f2a.local/api/all-off
    method: POST
    content_type: "application/json"
    payload: "{}"
    username: admin
    password: !secret bracelets_admin_password

template:
  - switch:
      - name: "Bracelets RF output"
        state: "{{ is_state('binary_sensor.bracelets_rf_output', 'on') }}"
        turn_on:
          action: rest_command.bracelets_output
          data: { enabled: "true" }
        turn_off:
          action: rest_command.bracelets_output
          data: { enabled: "false" }
```

## Automations

Kill the RF at the end of the show night:

```yaml
automation:
  - alias: "Bracelets off at midnight"
    triggers:
      - trigger: time
        at: "00:00:00"
    actions:
      - action: rest_command.bracelets_output
        data: { enabled: "false" }
```

Hold every bracelet red while a doorbell rings (test mode overrides xLights), then hand back to the show:

```yaml
automation:
  - alias: "Bracelets doorbell flash"
    triggers:
      - trigger: state
        entity_id: binary_sensor.doorbell
        to: "on"
    actions:
      - action: rest_command.bracelets_color
        data: { mode: "solid", rgb: "FF0000" }
      - delay: "00:00:10"
      - action: rest_command.bracelets_color
        data: { mode: "off", rgb: "000000" }
```

Notify when a controller stops receiving from xLights during show hours:

```yaml
automation:
  - alias: "Bracelets lost input"
    triggers:
      - trigger: state
        entity_id: binary_sensor.bracelets_receiving
        to: "off"
        for: "00:02:00"
    conditions:
      - condition: time
        after: "17:00:00"
        before: "23:00:00"
    actions:
      - action: notify.notify
        data:
          message: "Net2RF controller has had no show input for 2 minutes"
```

## Dashboard card

```yaml
type: entities
title: Bracelets
entities:
  - switch.bracelets_rf_output
  - binary_sensor.bracelets_receiving
  - sensor.bracelets_radio
  - sensor.bracelets_input_rate
  - sensor.bracelets_rf_airtime
  - sensor.bracelets_zone_1_colour
```

## Multiple controllers

Repeat the `rest:` block per controller (each has its own hostname). Every controller also lists the others at
`/api/discover`, and one of them (the lowest ID) answers to `net2rf.local`.
