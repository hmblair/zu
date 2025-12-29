# Remote Server Access Notes

## Preventing Tunnel Lockouts

If accessing a server via Cloudflare Tunnel (or similar), ensure you have a backup access method in case the tunnel goes down.

### Recommendations

1. **Static IP or DHCP Reservation**
   - Configure a static IP on the server, or
   - Set up a DHCP reservation on the router
   - This ensures the IP doesn't change if the server reboots

2. **Document Internal Hostname/IP**
   - Note the server's internal hostname and IP somewhere accessible
   - Add to `~/.ssh/config` as an alternative host entry

3. **Secondary Access Method**
   - Set up WireGuard or Tailscale as a backup VPN
   - These work independently of Cloudflare Tunnel
   - Can be used to restart `cloudflared` if it fails

4. **Monitoring/Alerts**
   - Set up uptime monitoring (e.g., UptimeRobot, Healthchecks.io)
   - Get alerted when the tunnel goes down

### Example SSH Config with Fallback

```
# Primary: via Cloudflare Tunnel
Host myserver
    HostName ssh.example.com
    ProxyCommand cloudflared access ssh --hostname %h

# Fallback: direct on VPN
Host myserver-internal
    HostName 10.x.x.x
    # or: HostName myserver.campus.edu
```
