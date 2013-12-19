wayland-keylogger
=================

This is a proof-of-concept Wayland keylogger that I wrote to demonstrate the fundamental insecurity of a typical Linux desktop that lacks both sandboxing (chroot, cgroups, ...) and mandatory access control (SELinux). The keylogger requires nothing but user-level access to be installed. Installation is very simple: just put the library somewhere in your home folder and LD_PRELOAD it from ~/.profile (or maybe ~/.bashrc), like this:

    export LD_PRELOAD=/home/user/path/to/libwayland-keylogger.so

This is only a proof of concept and it will simply print the key press/release events to stderr (your terminal). It would be relatively easy to make the keylogger invisible and write the key presses to a file.

I wrote this keylogger in 75 minutes, without any real knowledge of the Wayland protocol. This should give you an idea of how easy it is to write this. It is based on ElfHacks (written by Pyry Haulos, unrelated to this project) and parts of SSR-GLInject (written by me, also unrelated).

There are many ways to break this keylogger - it is not designed to be robust since that was not the goal. But for any possible way to break it, I could add countermeasures as well. Applications could use 'getenv' to check whether LD_PRELOAD is set, but I could overwrite 'getenv' to hide the existence of that variable. Applications could scan the /proc filesystem, but again I could overwrite the relevant system functions to hide it. I think it's pretty clear that this is not a secure system - a secure system would prevent the attack completely rather than trying to detect it after it has already happened. Such security mechanisms exist: even a few basic SELinux rules would completely eliminate this security problem.

The point that I am trying to make is that we should focus on *real* security mechanisms that actually *work*, rather than creating new ones that just give a false sense of security (and annoy the user) without actually making the desktop any more secure.

This program is in no way meant as criticism of the Wayland project. It simply demonstrates that creating a secure desktop requires more than just a few server-side restrictions.

By the way, this inherent weakness is not at all specific to Linux. Similar techniques would also work on Windows and Mac, and essentially any platform that doesn't sandbox applications.
