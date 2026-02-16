package main

import (
	"fmt"
	"log"
	"net"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"time"

	_ "github.com/BrandonKowalski/certifiable"
	gaba "github.com/BrandonKowalski/gabagool/v2/pkg/gabagool"
	"github.com/BrandonKowalski/gabagool/v2/pkg/gabagool/constants"
)

type Platform string

const (
	PlatformMac    Platform = "mac"
	PlatformTG5040 Platform = "tg5040"
	PlatformTG5050 Platform = "tg5050"
)

const sshPort = "22"

// Minimum stock OS version required for native SSH on TG5040.
const minTG5040Version = "1.1.1"

var platform Platform

func main() {
	platform = detectPlatform()

	logPath := getLogPath()
	gaba.Init(gaba.Options{
		WindowTitle:    "Native SSH",
		ShowBackground: true,
		LogPath:        logPath,
		IsNextUI:       platform != PlatformMac,
	})
	defer gaba.Close()

	// Check OS version requirement on TG5040
	if platform == PlatformTG5040 {
		if !checkOSVersion() {
			return
		}
	}

	runApp()
}

// checkOSVersion verifies the stock OS version on TG5040.
// Returns false if the version is too old and the user was warned.
func checkOSVersion() bool {
	version := readOSVersion()
	if version == "" {
		return true // can't determine, proceed anyway
	}

	if compareVersions(version, minTG5040Version) < 0 {
		msg := fmt.Sprintf(
			"Stock OS version %s detected.\n\nNative SSH requires version %s or higher.\nPlease update your stock OS.",
			version, minTG5040Version,
		)
		gaba.ConfirmationMessage(msg, []gaba.FooterHelpItem{
			{ButtonName: "B", HelpText: "Quit"},
		}, gaba.MessageOptions{})
		return false
	}

	return true
}

// readOSVersion reads /etc/version and returns the trimmed contents.
func readOSVersion() string {
	if platform == PlatformMac {
		return "" // not applicable
	}

	data, err := os.ReadFile("/etc/version")
	if err != nil {
		return ""
	}
	return strings.TrimSpace(string(data))
}

// compareVersions compares two dot-separated version strings.
// Returns -1 if a < b, 0 if a == b, 1 if a > b.
func compareVersions(a, b string) int {
	partsA := strings.Split(a, ".")
	partsB := strings.Split(b, ".")

	maxLen := len(partsA)
	if len(partsB) > maxLen {
		maxLen = len(partsB)
	}

	for i := 0; i < maxLen; i++ {
		var va, vb int
		if i < len(partsA) {
			fmt.Sscanf(partsA[i], "%d", &va)
		}
		if i < len(partsB) {
			fmt.Sscanf(partsB[i], "%d", &vb)
		}
		if va < vb {
			return -1
		}
		if va > vb {
			return 1
		}
	}
	return 0
}

func runApp() {
	for {
		sshEnabled := isSSHRunning()
		action := showStatusScreen(sshEnabled)

		switch action {
		case actionToggle:
			toggleSSH(!sshEnabled)
		case actionQuit:
			return
		}
	}
}

type screenAction int

const (
	actionQuit screenAction = iota
	actionToggle
)

func showStatusScreen(sshEnabled bool) screenAction {
	statusText := "Disabled"
	ipText := "-"
	portText := "-"

	if sshEnabled {
		statusText = "Enabled"
		ipText = getIPAddress()
		portText = sshPort
	}

	metadata := []gaba.MetadataItem{
		{Label: "Status", Value: statusText},
		{Label: "IP Address", Value: ipText},
		{Label: "Port", Value: portText},
	}

	if sshEnabled {
		metadata = append(metadata, gaba.MetadataItem{
			Label: "Connect",
			Value: fmt.Sprintf("ssh root@%s -p %s", ipText, portText),
		})
		metadata = append(metadata, gaba.MetadataItem{
			Label: "Password",
			Value: getDefaultPassword(),
		})
	}

	sections := []gaba.Section{
		gaba.NewInfoSection("SSH", metadata),
	}

	toggleText := "Enable"
	if sshEnabled {
		toggleText = "Disable"
	}

	options := gaba.DefaultInfoScreenOptions()
	options.Sections = sections
	options.ShowThemeBackground = true
	options.ShowScrollbar = false
	options.ConfirmButton = constants.VirtualButtonA

	footer := []gaba.FooterHelpItem{
		{ButtonName: "B", HelpText: "Quit"},
		{ButtonName: "A", HelpText: toggleText},
	}

	_, err := gaba.DetailScreen("Native SSH", options, footer)
	if err != nil {
		// ErrCancelled means user pressed B
		return actionQuit
	}

	// DetailScreen returned without error means A was pressed (confirmed)
	return actionToggle
}

// toggleSSH enables or disables SSH, showing a spinner while working.
func toggleSSH(enable bool) {
	action := "Enabling SSH..."
	if !enable {
		action = "Disabling SSH..."
	}

	gaba.ProcessMessage(action, gaba.ProcessMessageOptions{ShowThemeBackground: true}, func() (any, error) {
		if platform == PlatformMac {
			time.Sleep(1 * time.Second)
			return nil, nil
		}

		val := "0"
		if enable {
			val = "1"
		}

		// Toggle the system setting
		if err := exec.Command("/usr/trimui/bin/systemval", "enablessh", val).Run(); err != nil {
			log.Printf("systemval enablessh %s failed: %v", val, err)
		}

		if enable {
			// Start sshd
			if err := exec.Command("/usr/sbin/sshd").Run(); err != nil {
				log.Printf("sshd start: %v", err)
			}

			// Poll until sshd is running (up to 5 seconds)
			for i := 0; i < 10; i++ {
				if isSSHRunning() {
					break
				}
				time.Sleep(500 * time.Millisecond)
			}
		} else {
			// Stop all sshd processes
			exec.Command("killall", "-9", "sshd").Run()

			// Poll until sshd is stopped (up to 5 seconds)
			for i := 0; i < 10; i++ {
				if !isSSHRunning() {
					break
				}
				time.Sleep(500 * time.Millisecond)
			}
		}

		return nil, nil
	})
}

// isSSHRunning checks whether sshd is currently running.
func isSSHRunning() bool {
	if platform == PlatformMac {
		err := exec.Command("pgrep", "-x", "sshd").Run()
		return err == nil
	}

	err := exec.Command("pidof", "sshd").Run()
	return err == nil
}

// getIPAddress returns the first non-loopback IPv4 address.
func getIPAddress() string {
	ifaces, err := net.Interfaces()
	if err != nil {
		return "Unknown"
	}

	for _, iface := range ifaces {
		if iface.Flags&net.FlagUp == 0 || iface.Flags&net.FlagLoopback != 0 {
			continue
		}

		// On macOS, prefer en* interfaces
		if platform == PlatformMac && !strings.HasPrefix(iface.Name, "en") {
			continue
		}

		addrs, err := iface.Addrs()
		if err != nil {
			continue
		}

		for _, addr := range addrs {
			if ipnet, ok := addr.(*net.IPNet); ok {
				if ip4 := ipnet.IP.To4(); ip4 != nil {
					return ip4.String()
				}
			}
		}
	}

	return "No network"
}

// getDefaultPassword returns the default SSH password for the current platform.
func getDefaultPassword() string {
	if platform == PlatformTG5050 {
		return "(empty - no password)"
	}
	return "tina"
}

func detectPlatform() Platform {
	p := strings.ToUpper(os.Getenv("PLATFORM"))
	switch {
	case strings.Contains(p, "TG5050"):
		return PlatformTG5050
	case strings.Contains(p, "TG5040"), strings.Contains(p, "TG3040"):
		return PlatformTG5040
	default:
		if runtime.GOOS == "darwin" {
			return PlatformMac
		}
		return PlatformTG5040
	}
}

func getLogPath() string {
	if platform == PlatformMac {
		return filepath.Join(".", "nativessh.log")
	}

	userdata := os.Getenv("SHARED_USERDATA_PATH")
	if userdata == "" {
		home := os.Getenv("HOME")
		if home == "" {
			home = "/root"
		}
		userdata = filepath.Join(home, ".userdata")
	}

	logDir := filepath.Join(userdata, string(platform), "logs")
	return filepath.Join(logDir, "nativessh.log")
}
