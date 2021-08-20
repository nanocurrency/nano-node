# Security Policy

## Active Versions

The Banano network is designed to allow peering between multiple versions of the node software, with older versions being periodically de-peered. The active versions currently peering and being supported can be found in the Node Releases page of our documentation: https://docs.nano.org/releases/node-releases/

## Security Audit

In December 2018 the Banano node codebase was audited by Red4Sec and found to have no critical vulnerabilities. The following vulnerability was resolved:

**Risk**: High  
**Report Location**: Pages 34-35  
**Resolution**: [Pull Request #1563](https://github.com/nanocurrency/nano-node/pull/1563) in [release V17.1](https://github.com/nanocurrency/nano-node/releases/tag/V17.1)  
All other notices from the report were classified as informative and are continuously improved on over time (e.g. code styling). The full report is available here: https://content.nano.org/Nano_Final_Security_Audit_v3.pdf

## Reporting a Vulnerability

To report security issues in the Nano protocol, please send an email to security@nano.org and CC the following security team members. It is strongly recommended to encrypt the email using GPG and the pubkeys below can be used for this purpose.

| GitHub Username | Email | GPG Pubkey |
|-----------------------|--------|-----------------|
| [clemahieu](https://github.com/clemahieu) | clemahieu { at } gmail.com | [clemahieu.asc](https://github.com/BananoCoin/banano/blob/develop/etc/gpg/clemahieu.asc) |
| [argakiig](https://github.com/argakiig) | russel { at } nano.org | [argakiig.asc](https://github.com/BananoCoin/banano/blob/develop/etc/gpg/argakiig.asc) |
| [wezrule](https://github.com/wezrule) | wezrule { at } hotmail.com | [wezrule.asc](https://github.com/BananoCoin/banano/blob/develop/etc/gpg/wezrule.asc) |
| [sergiysw](https://github.com/sergiysw) | sergiysw { at } gmail.com | [sergiysw.asc](https://github.com/BananoCoin/banano/blob/develop/etc/gpg/sergiysw.asc) |
| [zhyatt](https://github.com/zhyatt) | zach { at } nano.org | [zhyatt.asc](https://github.com/BananoCoin/banano/blob/develop/etc/gpg/zhyatt.asc) |

For details on how to send a GPG encrypted email, see the tutorial here: https://www.linode.com/docs/security/encryption/gpg-keys-to-send-encrypted-messages/.

For general support and other non-sensitive inquiries, please visit https://forum.nano.org.
