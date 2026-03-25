# obeliskd

`obeliskd` is the D-based (`-betterC`) init system for Obelisk.

## Config

Path: `/etc/obelisk/initd.conf`

Format: `key=value`

Supported keys:

- `profile` - boot profile label
- `verbose` - `yes/no`
- `services` - comma-separated list of unit files
- `default_user` - login user name
- `default_uid` - fallback uid
- `default_gid` - fallback gid
- `shell` - login shell path
- `shell_arg0` - shell argv[0]
- `shell_arg1` - shell argv[1]

## Service unit syntax

Path: `/etc/obelisk/initd/services/*.svc`

Format: `key=value`

Supported keys:

- `name`
- `description`
- `exec`
- `autostart` (`yes/no`)
- `oneshot` (`yes/no`)
- `restart` (`never` or `on-failure`)
- `user`
- `uid`
- `gid`

## Safety

- If primary shell launch fails, `obeliskd` falls back to `/sbin/init-legacy`.
- If legacy init fails, it falls back to `/bin/osh -i`.
