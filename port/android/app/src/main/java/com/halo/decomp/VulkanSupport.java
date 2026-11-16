package com.halo.decomp;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.pm.PackageManager;

/**
 * Whether this phone has the Vulkan 1.3 the renderer needs
 * (port/linux/vulkan). The manifest names it (uses-feature
 * android.hardware.vulkan.version), but only a store enforces that, and the
 * app is installed from its APK and updates itself (Updater): so the
 * launcher asks before it starts the game, and the updater before it
 * offers a new version, rather than leave the player a black screen.
 */
final class VulkanSupport {
    /** Vulkan 1.3.0, as the feature's version encodes it (major << 22 | minor << 12) */
    private static final int VULKAN_1_3 = 0x403000;

    private VulkanSupport() {
    }

    static boolean present(Activity activity) {
        return activity.getPackageManager().hasSystemFeature(PackageManager.FEATURE_VULKAN_HARDWARE_VERSION,
            VULKAN_1_3);
    }

    /** says the phone cannot run the game; then runs after (null: nothing) */
    static void explain(Activity activity, Runnable after) {
        if (activity.isFinishing()) {
            if (after != null)
                after.run();
            return;
        }
        new AlertDialog.Builder(activity)
            .setTitle("GOAT H1 cannot run on this device")
            .setMessage("The game draws with Vulkan 1.3, which this device's graphics driver does not have.\n\n"
                + "Most phones from about 2021 on have it, once their system updates are installed. If an "
                + "update for this device is offered, install it and try again.")
            .setCancelable(false)
            .setPositiveButton("OK", (dialog, which) -> {
                if (after != null)
                    after.run();
            })
            .show();
    }
}
