package com.halo.decomp;

import android.app.AlertDialog;
import android.content.Context;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.content.res.Configuration;
import android.content.res.Resources;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.BlurMaskFilter;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.LightingColorFilter;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.Picture;
import android.graphics.RadialGradient;
import android.graphics.RectF;
import android.graphics.Shader;
import android.graphics.Typeface;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.hardware.input.InputManager;
import android.media.AudioAttributes;
import android.os.Build;
import android.os.SystemClock;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.os.VibratorManager;
import android.util.SparseArray;
import android.util.SparseIntArray;
import android.view.InputDevice;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.Switch;
import android.widget.TextView;
import android.widget.Toast;

import java.util.Arrays;
import java.util.HashMap;
import java.util.Locale;

/**
 * The on-screen touch controls: a multitouch overlay over SDL's surface,
 * with a stick, the controller's buttons, swipe (and optionally gyroscope)
 * aiming and an editor for the layout (TouchLayout). Its state goes to the
 * game through port/android/host/host_touch.c, as port 0's controller.
 *
 * It shows only in a game, never at the main menu, over a menu (which
 * takes taps itself, port/linux/src/touch_input.c) or during a cinematic,
 * only on a touchscreen, and with input.touch_controls = "auto" only while
 * no controller is connected and no mouse or keyboard is plugged in or
 * paired (the phone's own keys and touchpad do not count). While it is not
 * shown it lets every finger through to the game.
 */
public final class TouchControls extends View implements SensorEventListener, InputManager.InputDeviceListener {
    /** what a finger holds besides a control: the view swipe */
    private static final int LOOK = -5;
    private static final int NOTHING = Integer.MIN_VALUE;
    /** the toolbar's items (at the top of the screen) */
    private static final int TOOLBAR_TOGGLE = -3;
    private static final int TOOLBAR_OPTIONS = -4;
    private static final int TOOLBAR_EXPORT = -6;
    private static final int TOOLBAR_IMPORT = -7;
    private static final float TOOLBAR_RADIUS = 28;
    private static final float OPTIONS_RADIUS = 34;

    /** the logical grid the controls are laid out on, scaled to the display's height */
    private static final float GRID_WIDTH = 960;
    private static final float GRID_HEIGHT = 540;
    /** a control is never smaller than a finger: 24 dp of radius, 48 across */
    private static final float MINIMUM_RADIUS_DP = 24;
    /** a finger moves this far (logical units) before a press becomes a swipe */
    private static final float SLOP = 10;
    /** the move stick's dead zone and its touch area beyond its circle */
    private static final float STICK_DEAD_ZONE = 0.12f;
    private static final float STICK_REACH = 1.28f;
    private static final float STICK_KNOB_RADIUS = 24;
    /**
     * a floating stick goes where a thumb lands in this part of the display,
     * from the lower left: a share of its width, and logical units up from
     * the bottom (65% of a 16:9 display's height, as low on a taller one)
     */
    private static final float FLOATING_ZONE_WIDTH = 0.45f;
    private static final float FLOATING_ZONE_HEIGHT = 351;
    /** radians per logical unit of swipe (the mouse's rate: xinput_sdl.c) */
    private static final float LOOK_RADIANS_PER_UNIT = 0.0022f;
    /**
     * how solid each part is drawn, times the layout's opacity: the game
     * shows through everything, the seldom used controls more
     */
    private static final float BUTTON_ALPHA = 0.8f;
    private static final float MINOR_BUTTON_ALPHA = 0.55f;
    private static final float ICON_ALPHA = 0.85f;
    private static final float MINOR_ICON_ALPHA = 0.6f;
    private static final float STICK_RING_ALPHA = 0.35f;
    private static final float STICK_KNOB_ALPHA = 0.55f;
    /**
     * a controller button's art (with its edge and shadow), half its longer
     * side in its control's radii; a line icon's; either sinks a little and
     * brightens while held
     */
    private static final float ART_SIZE = 1f;
    private static final float ICON_SIZE = 0.62f;
    private static final float MINOR_ICON_SIZE = 0.56f;
    private static final float PRESSED_SIZE = 0.9f;
    /** the line icons', the stick's and the captions' colour: the HUD's light blue (tools/touch_assets.py) */
    private static final int HUD_COLOR = 0xff9cd3ff;
    /** a caption's size and how solid it is (input.touch_captions) */
    private static final float CAPTION_SIZE = 9;
    private static final float CAPTION_ALPHA = 0.85f;
    /**
     * controls nobody has touched for IDLE_MS fade to IDLE_OPACITY of the
     * opacity setting, over FADE_OUT_MS; a finger brings them back over
     * FADE_IN_MS
     */
    private static final long IDLE_MS = 3000;
    private static final float IDLE_OPACITY = 0.7f;
    /** a glow reaches this far past its art, in the art's longer side */
    private static final float GLOW_MARGIN = 0.25f;
    /** ... from its art's shape this much larger */
    private static final float GLOW_GROW = 0.14f;
    private static final float FADE_OUT_MS = 600;
    private static final float FADE_IN_MS = 120;
    /** a label is at most this many radii wide */
    private static final float LABEL_WIDTH = 1.7f;
    /** the labels' fitted sizes remembered (a few dozen are drawn; the editor's sizes add more) */
    private static final int LABEL_FITS = 256;
    private static final long POLL_MS = 16;
    /** the game is in play this long before the controls show (no flash at a level's first frame) */
    private static final long SHOW_DELAY_MS = 200;
    private static final long VIBRATION_MS = 110;
    private static final long VIBRATION_RENEW_MS = 70;

    /** the game's HALO_TOUCH_SCENE bits (port/linux/src/touch_input.c) */
    private static final int SCENE_KNOWN = 1;
    private static final int SCENE_MENUS = 2;
    private static final int SCENE_ON = 4;
    private static final int SCENE_OFF = 8;
    /** the game sees a controller of its own (SDL_GetGamepads) */
    private static final int SCENE_CONTROLLER = 16;
    /** display.hud_button_style (0 classic, 1 gems, 2 menu_gems) and input.touch_captions */
    private static final int SCENE_STYLE_SHIFT = 5;
    private static final int SCENE_CAPTIONS = 128;
    /** display.frame_style (0 plain, 1 halo, 2 xbox) and display.picture_shape "16:9", for portrait */
    private static final int SCENE_FRAME_SHIFT = 16;
    private static final int SCENE_WIDE_PICTURE = 1 << 18;
    private static final int FRAME_HALO = 1;
    /** the frame's accents: the HUD's cyan (HUD_COLOR) for halo, a green for xbox */
    private static final int FRAME_GREEN = 0xff78cd3c;
    /** input.touch_crouch "toggle" (none: "hold") */
    private static final int SCENE_CROUCH_TOGGLE = 1 << 19;

    private static final String PREFERENCES = "touch-layout-v1";
    private static final String KEY_CONFIGURATION = "configuration";
    /** the portrait layout's (display.orientation "portrait", a portrait-shaped window) */
    private static final String KEY_PORTRAIT_CONFIGURATION = "configuration-portrait";
    private static final String KEY_HIDDEN = "hidden";

    /**
     * a kind of control: the SDL button bit or trigger axis it sends (-1 for
     * none), and the game's controller button it is (port/linux/game/
     * touch_game.c numbers them: input.h's _gamepad_*), which names it; a
     * D-pad button's arrow points this many degrees clockwise from the right
     * (-1 for none)
     */
    private static final class Kind {
        final int bit;
        final int trigger;
        final int gamepadButton;
        final String buttonName;
        final boolean faceButton;
        final int arrow;

        Kind(int bit, int trigger, int gamepadButton, String buttonName, boolean faceButton) {
            this(bit, trigger, gamepadButton, buttonName, faceButton, -1);
        }

        Kind(int bit, int trigger, int gamepadButton, String buttonName, boolean faceButton, int arrow) {
            this.bit = bit;
            this.trigger = trigger;
            this.gamepadButton = gamepadButton;
            this.buttonName = buttonName;
            this.faceButton = faceButton;
            this.arrow = arrow;
        }
    }

    /** by TouchLayout's kinds; bits follow SDL_GamepadButton, triggers are SDL axes 4 and 5 */
    private static final Kind[] KINDS = {
        new Kind(0, -1, 0, "A", true),
        new Kind(1, -1, 1, "B", true),
        new Kind(2, -1, 2, "X", true),
        new Kind(3, -1, 3, "Y", true),
        new Kind(-1, 5, 7, "RT", false),
        new Kind(-1, 4, 6, "LT", false),
        new Kind(7, -1, 14, "LS", false),
        new Kind(8, -1, 15, "RS", false),
        new Kind(9, -1, 5, "White", false),
        new Kind(10, -1, 4, "Black", false),
        new Kind(6, -1, 12, "Start", false),
        new Kind(4, -1, 13, "Back", false),
        new Kind(11, -1, 8, "Up", false, 270),
        new Kind(12, -1, 9, "Down", false, 90),
        new Kind(13, -1, 10, "Left", false, 180),
        new Kind(14, -1, 11, "Right", false, 0),
        new Kind(-1, -1, -1, "Move", false),
        new Kind(-1, 5, 7, "RT", false),
    };

    /** the game's controls (input_abstraction.c's _game_control_*), as the options' lists name them */
    private static final String[] GAME_CONTROLS = {
        "Jump", "Gren. type", "Reload", "Weapon", "Melee", "Light",
        "Grenade", "Fire", "Pause", "Back", "Crouch", "Zoom",
    };
    /**
     * the controller's buttons drawn as the HUD draws them (port/assets/
     * buttons, through tools/touch_assets.py: port/assets/touch/README.md):
     * A, B, X, Y, black and white in each display.hud_button_style ("gems"
     * has menu_gems' black and white), then the triggers and the sticks
     */
    private static final int[][] FACES = {
        {R.drawable.touch_button_classic_a, R.drawable.touch_button_classic_b, R.drawable.touch_button_classic_x,
            R.drawable.touch_button_classic_y, R.drawable.touch_button_classic_black,
            R.drawable.touch_button_classic_white},
        {R.drawable.touch_button_gems_a, R.drawable.touch_button_gems_b, R.drawable.touch_button_gems_x,
            R.drawable.touch_button_gems_y, R.drawable.touch_button_menu_black, R.drawable.touch_button_menu_white},
        {R.drawable.touch_button_menu_a, R.drawable.touch_button_menu_b, R.drawable.touch_button_menu_x,
            R.drawable.touch_button_menu_y, R.drawable.touch_button_menu_black, R.drawable.touch_button_menu_white},
    };
    private static final int[] CONTROL_ART = {
        R.drawable.touch_trigger_left, R.drawable.touch_trigger_right,
        R.drawable.touch_thumbstick_left, R.drawable.touch_thumbstick_right,
    };
    /**
     * by TouchLayout's kinds: the art each is drawn with (0..5 a face of
     * FACES, 6..9 CONTROL_ART, -1 a line icon), and whether it is seldom
     * used, drawn smaller and fainter (the stick clicks, white, black, back,
     * the D-pad)
     */
    private static final int[] KIND_ART = {
        0, 1, 2, 3, 7, 6, 8, 9, 5, 4, -1, -1, -1, -1, -1, -1, -1, 7,
    };
    private static final boolean[] MINOR_KINDS = {
        false, false, false, false, false, false, true, true, true, true, false, true,
        true, true, true, true, false, false,
    };
    /** ... as the captions under the buttons name them */
    private static final String[] CAPTIONS = {
        "JUMP", "GREN TYPE", "RELOAD", "SWAP", "MELEE", "LIGHT",
        "GRENADE", "FIRE", "PAUSE", "BACK", "CROUCH", "ZOOM",
    };
    /** the default profile's mapping, until the game sends the player's own */
    private static final int[] DEFAULT_BINDINGS = {
        0, 4, 2, 3, 1, 5, 6, 7, -1, -1, -1, -1, 8, 9, 10, 11,
    };

    private static native void nativeState(int lx, int ly, int rx, int ry, int lt, int rt, int buttons);
    private static native void nativeLook(float dx, float dy);
    private static native void nativeGyro(float dx, float dy);
    private static native void nativeLookReset();
    private static native int nativeRumble();
    private static native int nativeScene();
    private static native int nativeBindings(int[] controls);
    private static native void nativePictureTop(int pixels);
    private static native void nativeDevices(boolean touchscreen, boolean controller);

    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint pictures = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
    private final RectF pictureBounds = new RectF();
    /** a held controller button's art, a little brighter */
    private final LightingColorFilter pressed = new LightingColorFilter(0xffffffff, 0x00262626);
    /** the pictures the controls are drawn with: the HUD's buttons, and line icons of Kenney's Mobile Controls */
    private final Bitmap[][] faces = new Bitmap[FACES.length][];
    private final Bitmap[] controlArt = new Bitmap[CONTROL_ART.length];
    private final Bitmap pauseIcon;
    private final Bitmap backIcon;
    private final Bitmap arrowIcon;
    private final Bitmap optionsIcon;
    private final Bitmap hideIcon;
    private final Bitmap showIcon;
    /** the captions' font: Overpass, as the HUD's text (res/font, tools/touch_assets.py) */
    private final Typeface captionFont;
    /** each button art's glow (TouchContext.glow), made when first drawn */
    private final HashMap<Bitmap, Bitmap> glows = new HashMap<>();
    /** the idle fade (IDLE_OPACITY..1), and when a finger last touched the screen */
    private float idle = 1f;
    private long lastTouch;
    private long lastFade;
    private final SharedPreferences preferences;
    private final SensorManager sensors;
    private final Sensor gyroscope;
    private final Vibrator vibrator;
    private final InputManager inputManager;
    private final boolean touchscreenFeature;
    private final GyroscopeAim gyroAim = new GyroscopeAim();
    private final float[] gyroDelta = new float[2];
    private final AudioAttributes rumbleAttributes = new AudioAttributes.Builder()
        .setUsage(AudioAttributes.USAGE_GAME)
        .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
        .build();

    /**
     * the layouts of a landscape view and of a portrait one (taller than it
     * is wide: the game's picture at the top, the controls below it), each
     * edited and saved on its own; layout is the one in use (layoutControls)
     */
    private TouchLayout landscapeLayout = new TouchLayout(false);
    private TouchLayout portraitLayout = new TouchLayout(true);
    private TouchLayout layout = landscapeLayout;
    private float sensitivity = 1f;
    /** the controls removed by the toolbar's Hide, until its Touch */
    private boolean hidden;
    private boolean editing;
    /** the option dialogs open (one opens before the last one's dismissal is told) */
    private int openDialogs;

    /** what each finger holds: a control, LOOK, or (in toolbarFingers) a toolbar item */
    private final SparseIntArray owners = new SparseIntArray();
    /** where each finger on a button went down, in pixels: dragged, it swipes */
    private final SparseArray<float[]> buttonTouches = new SparseArray<>();
    /** each finger on a toolbar item: {x, y, item}; the item acts when it lifts there */
    private final SparseArray<float[]> toolbarFingers = new SparseArray<>();
    private final int[] axes = new int[6];
    private int lookPointer = -1;
    private float lookX;
    private float lookY;
    /**
     * the kind of control that crouches (the one bound to Crouch), tapped
     * on with input.touch_crouch "toggle": it stays down while the stick
     * moves, until it is tapped again, Jump is pressed, a vehicle's seat is
     * taken or the player dies; -1 standing (TouchContext.crouchPressed,
     * crouchKept). (Crouch sits under the left thumb beside the stick, where
     * it cannot be held while moving; the Xbox's is the stick's own click.)
     */
    private int crouchedKind = -1;
    /** a floating stick's centre while a thumb holds it (logical units) */
    private boolean stickFloating;
    private float stickX;
    private float stickY;
    private int dragPointer = -1;
    private int dragControl = -1;
    private float dragOffsetX;
    private float dragOffsetY;

    /** pixels per logical unit, and the display in logical units */
    private float scale = 1;
    private float logicalWidth = GRID_WIDTH;
    private float logicalHeight = GRID_HEIGHT;
    private int insetRight;
    private int insetTop;
    /** the camera cutout's top inset, in pixels: portrait's strip stays under it */
    private int cutoutTop;
    /** portrait: where the game's picture is, in logical units (empty in landscape) */
    private final RectF pictureRect = new RectF();
    /** the portrait frame, drawn once for its look and the picture's place (null: draw it again) */
    private Picture frame;
    private int frameStyle;

    private int scene;
    private boolean controllerConnected;
    private boolean mouseConnected;
    private boolean keyboardConnected;
    /** the density the controls were last laid out for (onConfigurationChanged) */
    private int densityDpi;
    private boolean touchscreen;
    private boolean deviceInputActive;
    /** the controls are on screen: shown() for SHOW_DELAY_MS */
    private boolean showing;
    private long shownSince = -1;
    private boolean gyroRegistered;
    private int lastAmplitude;
    private long lastVibration;
    private final int[] bindings = DEFAULT_BINDINGS.clone();
    private final int[] bindingsRead = new int[bindings.length];
    private int bindingsSerial;
    /** each kind's label, made once for the bindings (null: not yet) */
    private final String[] labels = new String[KINDS.length];

    /**
     * a label's text size, made smaller to fit its circle: measureText is
     * slow, and every frame of a touch draws every label at the same sizes
     */
    private static final class LabelFit {
        String label;
        float radius;
        float textSize;
        float fitted;

        LabelFit set(String label, float radius, float textSize) {
            this.label = label;
            this.radius = radius;
            this.textSize = textSize;
            return this;
        }

        @Override
        public boolean equals(Object other) {
            if (!(other instanceof LabelFit))
                return false;
            LabelFit fit = (LabelFit) other;
            return label.equals(fit.label) && Float.floatToIntBits(radius) == Float.floatToIntBits(fit.radius)
                && Float.floatToIntBits(textSize) == Float.floatToIntBits(fit.textSize);
        }

        @Override
        public int hashCode() {
            return (label.hashCode() * 31 + Float.floatToIntBits(radius)) * 31 + Float.floatToIntBits(textSize);
        }
    }

    private final HashMap<LabelFit, LabelFit> labelFits = new HashMap<>();
    private final LabelFit labelFitKey = new LabelFit();

    /** reads what the game says every frame: when to show, the buttons' names, and the rumble */
    private final Runnable poll = new Runnable() {
        @Override
        public void run() {
            if (!deviceInputActive)
                return;
            int was = scene;
            scene = nativeScene();
            // (the buttons' style and captions are settings: drawn again when they change)
            if (((was ^ scene) & (3 << SCENE_STYLE_SHIFT | SCENE_CAPTIONS)) != 0
                    || TouchContext.fromScene(was) != TouchContext.fromScene(scene)
                    || (showing && !hidden && TouchContext.animating(TouchContext.fromScene(scene))))
                invalidate();
            // (the picture's shape and the frame's look: placed and drawn again)
            if (((was ^ scene) & (3 << SCENE_FRAME_SHIFT | SCENE_WIDE_PICTURE | SCENE_KNOWN)) != 0)
                placePicture();
            // (a crouch tapped on stands up at death or in a seat, with no finger down)
            if (((was ^ scene) & SCENE_CROUCH_TOGGLE) != 0
                    || TouchContext.fromScene(was) != TouchContext.fromScene(scene)) {
                publish();
                invalidate();
            }
            fadeWhenIdle();
            updateShowing();
            readBindings();
            updateRumble();
            postDelayed(this, POLL_MS);
        }
    };

    public TouchControls(Context context) {
        super(context);
        Resources resources = context.getResources();
        pauseIcon = picture(resources, R.drawable.touch_icon_pause);
        backIcon = picture(resources, R.drawable.touch_icon_back);
        arrowIcon = picture(resources, R.drawable.touch_icon_arrow);
        optionsIcon = picture(resources, R.drawable.touch_icon_options);
        hideIcon = picture(resources, R.drawable.touch_icon_hide);
        showIcon = picture(resources, R.drawable.touch_icon_show);
        captionFont = resources.getFont(R.font.touch_caption);
        lastTouch = SystemClock.uptimeMillis();
        for (int style = 0; style < FACES.length; style++) {
            faces[style] = new Bitmap[FACES[style].length];
            for (int i = 0; i < FACES[style].length; i++)
                faces[style][i] = picture(resources, FACES[style][i]);
        }
        for (int i = 0; i < CONTROL_ART.length; i++)
            controlArt[i] = picture(resources, CONTROL_ART[i]);
        sensors = (SensorManager) context.getSystemService(Context.SENSOR_SERVICE);
        gyroscope = sensors == null ? null : sensors.getDefaultSensor(Sensor.TYPE_GYROSCOPE);
        if (Build.VERSION.SDK_INT >= 31) {
            VibratorManager manager = (VibratorManager) context.getSystemService(Context.VIBRATOR_MANAGER_SERVICE);
            vibrator = manager == null ? null : manager.getDefaultVibrator();
        } else {
            vibrator = (Vibrator) context.getSystemService(Context.VIBRATOR_SERVICE);
        }
        inputManager = (InputManager) context.getSystemService(Context.INPUT_SERVICE);
        touchscreenFeature = context.getPackageManager().hasSystemFeature(PackageManager.FEATURE_TOUCHSCREEN);
        preferences = context.getSharedPreferences(PREFERENCES, Context.MODE_PRIVATE);
        loadLayout(context);
        hidden = preferences.getBoolean(KEY_HIDDEN, false);
        refreshInputDevices();
        setFocusable(false);
        setContentDescription("Halo touch controller");
        setOnApplyWindowInsetsListener((view, insets) -> {
            // (only the toolbar moves: the fingers stay held as the system bars come and go)
            int right = insetRight;
            int top = insetTop;
            insetRight = insets.getSystemWindowInsetRight();
            insetTop = insets.getSystemWindowInsetTop();
            int cutout = cutoutTop;
            cutoutTop = 0;
            if (insets.getDisplayCutout() != null) {
                insetRight = Math.max(insetRight, insets.getDisplayCutout().getSafeInsetRight());
                insetTop = Math.max(insetTop, insets.getDisplayCutout().getSafeInsetTop());
                cutoutTop = insets.getDisplayCutout().getSafeInsetTop();
            }
            // (portrait's strip, the controls in it and the picture under it move with the cutout alone)
            if (cutoutTop != cutout)
                layoutControls();
            else if (insetRight != right || insetTop != top)
                invalidate();
            return insets;
        });
    }

    /** a picture, drawn at many sizes from one large one (res/drawable-nodpi) */
    private static Bitmap picture(Resources resources, int id) {
        Bitmap bitmap = BitmapFactory.decodeResource(resources, id);
        bitmap.setHasMipMap(true);
        return bitmap;
    }

    private void loadLayout(Context context) {
        String configuration = preferences.getString(KEY_CONFIGURATION, null);
        String portrait = preferences.getString(KEY_PORTRAIT_CONFIGURATION, null);
        // (each read apart: one that cannot be read leaves the other as saved)
        boolean unreadable = false;
        try {
            if (configuration != null) {
                TouchLayout.Configuration saved = TouchLayout.importConfiguration(configuration);
                if (!saved.layout.portrait) {
                    landscapeLayout = saved.layout;
                    sensitivity = saved.sensitivity;
                }
            }
        } catch (IllegalArgumentException e) {
            unreadable = true;
        }
        // (none saved yet: the portrait defaults, with the settings the landscape layout has)
        portraitLayout.takeSettings(landscapeLayout);
        try {
            if (portrait != null) {
                TouchLayout.Configuration saved = TouchLayout.importConfiguration(portrait);
                if (saved.layout.portrait)
                    portraitLayout = saved.layout;
            }
        } catch (IllegalArgumentException e) {
            unreadable = true;
        }
        if (unreadable)
            Toast.makeText(context, "The saved touch layout could not be read. Using the default one.",
                Toast.LENGTH_LONG).show();
        layout = landscapeLayout;
    }

    // ---------- the activity's lifecycle (HaloActivity)

    public void startDeviceInput() {
        if (deviceInputActive)
            return;
        deviceInputActive = true;
        if (inputManager != null)
            inputManager.registerInputDeviceListener(this, null);
        refreshInputDevices();
        gyroAim.reset();
        updateSensors();
        post(poll);
    }

    public void stopDeviceInput() {
        deviceInputActive = false;
        removeCallbacks(poll);
        if (inputManager != null)
            inputManager.unregisterInputDeviceListener(this);
        updateSensors();
        cancelRumble();
        reset();
    }

    // ---------- the display

    @Override
    protected void onSizeChanged(int width, int height, int oldWidth, int oldHeight) {
        layoutControls();
        requestApplyInsets();
    }

    /**
     * a new density, perhaps at the same size (the activity handles density
     * in its configChanges, as a folding phone's two screens can differ):
     * the finger's width in logical units changes
     */
    @Override
    protected void onConfigurationChanged(Configuration configuration) {
        super.onConfigurationChanged(configuration);
        if (configuration.densityDpi != densityDpi)
            layoutControls();
    }

    /** scales the grid to the display (its height, keeping the circles round) */
    private void layoutControls() {
        if (getWidth() <= 0 || getHeight() <= 0)
            return;
        reset();
        densityDpi = getResources().getConfiguration().densityDpi;
        float width = getWidth();
        float height = getHeight();
        // (a portrait view has the portrait layout, its settings the ones in use)
        boolean portrait = height > width;
        if (portrait != layout.portrait) {
            TouchLayout next = portrait ? portraitLayout : landscapeLayout;
            next.takeSettings(layout);
            layout = next;
        }
        scale = Math.max(0.01f, Math.min(width / layout.gridWidth(), height / layout.gridHeight()));
        logicalWidth = width / scale;
        logicalHeight = height / scale;
        layout.bounds(logicalWidth, logicalHeight);
        layout.setTopInset(portrait ? cutoutTop / scale : 0);
        layout.setMinimumRadius(minimumRadius());
        placePicture();
    }

    /**
     * portrait: where the game's picture goes (under the strip, lowered into
     * the space above the controls: TouchLayout.pictureTop), told to the
     * game, which draws it there (host_touch_picture_top); landscape: none,
     * the game places it itself
     */
    private void placePicture() {
        if (!layout.portrait || getWidth() <= 0) {
            pictureRect.setEmpty();
            nativePictureTop(-1);
        } else {
            boolean wide = (scene & SCENE_WIDE_PICTURE) != 0;
            int top = Math.round(layout.pictureTop(TouchLayout.pictureHeight(logicalWidth, wide)) * scale);
            // (as the game's present: whole pixels, the picture no lower than the bottom)
            int height = getWidth() * 480 / (wide ? 852 : 640);
            top = Math.max(0, Math.min(top, getHeight() - height));
            pictureRect.set(0, top / scale, logicalWidth, (top + height) / scale);
            layout.setPictureTop(top / scale);
            nativePictureTop(top);
        }
        frame = null;
        invalidate();
    }

    /** a finger's width, in logical units (at most TouchLayout.MAX_MINIMUM_RADIUS) */
    private float minimumRadius() {
        float radius = MINIMUM_RADIUS_DP * getResources().getDisplayMetrics().density / scale;
        return Math.min(radius, TouchLayout.MAX_MINIMUM_RADIUS);
    }

    private float logical(float pixels) {
        return pixels / scale;
    }

    private float toolbarY() {
        // (portrait: in the strip, with back and pause)
        if (layout.portrait)
            return layout.stripBottom() - TouchLayout.PORTRAIT_STRIP / 2;
        return Math.max(38, insetTop / scale + 38);
    }

    /**
     * the toolbar's hide: in the middle, or in portrait beside the options
     * (a camera's cutout is in the middle), but with the picture behind the
     * strip right of the middle, beside pause, off the HUD's shields
     */
    private float hideX() {
        if (layout.portrait)
            return layout.stripGathered() ? logicalWidth / 2 + TouchLayout.GATHERED_APART : optionsX() - 70;
        return logicalWidth / 2;
    }

    private float optionsX() {
        return logicalWidth - Math.max(42, insetRight / scale + 42);
    }

    private float toolbarRadius(float radius) {
        return Math.max(radius, minimumRadius());
    }

    // ---------- when the controls show

    /**
     * whether the controls belong on screen: in a game (no menu or
     * cinematic), never without a touchscreen, and with
     * input.touch_controls = "auto" only while no controller, mouse or
     * keyboard is connected (the game says which controllers SDL has, as
     * this view's device list misses some, a pad paired after the game
     * started)
     */
    private boolean shown() {
        if ((scene & SCENE_KNOWN) == 0 || (scene & (SCENE_MENUS | SCENE_OFF)) != 0 || !touchscreen)
            return false;
        return (scene & SCENE_ON) != 0
            || (!controllerConnected && !mouseConnected && !keyboardConnected && (scene & SCENE_CONTROLLER) == 0);
    }

    /** shows the controls once shown() has held for SHOW_DELAY_MS; hides them at once */
    private void updateShowing() {
        boolean show = false;
        if (!shown()) {
            shownSince = -1;
        } else {
            long now = SystemClock.uptimeMillis();
            if (shownSince < 0)
                shownSince = now;
            show = now - shownSince >= SHOW_DELAY_MS;
        }
        if (show != showing) {
            showing = show;
            reset();
            updateSensors();
        }
    }

    /** a game controller: a stick or a hat, not a phone's few gamepad keys */
    private static boolean isController(InputDevice device) {
        if (device == null || device.isVirtual())
            return false;
        if ((device.getSources() & InputDevice.SOURCE_JOYSTICK) != InputDevice.SOURCE_JOYSTICK)
            return false;
        return device.getMotionRange(MotionEvent.AXIS_X, InputDevice.SOURCE_JOYSTICK) != null
            || device.getMotionRange(MotionEvent.AXIS_HAT_X, InputDevice.SOURCE_JOYSTICK) != null;
    }

    /**
     * a mouse (or a trackpad) plugged in or paired: an external SOURCE_MOUSE
     * device, not a phone's own touchpad (a Galaxy's sec_touchpad says
     * SOURCE_MOUSE too)
     */
    private static boolean isMouse(InputDevice device) {
        if (device == null || device.isVirtual() || !isExternal(device))
            return false;
        return (device.getSources() & InputDevice.SOURCE_MOUSE) == InputDevice.SOURCE_MOUSE;
    }

    /**
     * a keyboard plugged in or paired: an external SOURCE_KEYBOARD device
     * with letters, not a phone's own keys (power, volume) or touchscreen,
     * which say SOURCE_KEYBOARD too
     */
    private static boolean isKeyboard(InputDevice device) {
        if (device == null || device.isVirtual() || !isExternal(device))
            return false;
        return (device.getSources() & InputDevice.SOURCE_KEYBOARD) == InputDevice.SOURCE_KEYBOARD
            && device.getKeyboardType() == InputDevice.KEYBOARD_TYPE_ALPHABETIC;
    }

    /**
     * not built into the device: InputDevice.isExternal() from Android 10;
     * on Android 9, anything without a touchscreen or touchpad source (a
     * touchpad paired on Android 9 does not hide the controls)
     */
    private static boolean isExternal(InputDevice device) {
        if (Build.VERSION.SDK_INT >= 29)
            return device.isExternal();
        int sources = device.getSources();
        return (sources & InputDevice.SOURCE_TOUCHSCREEN) != InputDevice.SOURCE_TOUCHSCREEN
            && (sources & InputDevice.SOURCE_TOUCHPAD) != InputDevice.SOURCE_TOUCHPAD;
    }

    private void refreshInputDevices() {
        boolean controller = false;
        boolean mouse = false;
        boolean keyboard = false;
        boolean touch = touchscreenFeature;
        for (int id : InputDevice.getDeviceIds()) {
            InputDevice device = InputDevice.getDevice(id);
            if (device == null)
                continue;
            if (isController(device))
                controller = true;
            if (isMouse(device))
                mouse = true;
            if (isKeyboard(device))
                keyboard = true;
            boolean touchSource = (device.getSources() & InputDevice.SOURCE_TOUCHSCREEN) == InputDevice.SOURCE_TOUCHSCREEN;
            if (!device.isVirtual() && touchSource)
                touch = true;
        }
        controllerConnected = controller;
        mouseConnected = mouse;
        keyboardConnected = keyboard;
        touchscreen = touch;
        // (the game's own touchscreen buttons follow the same rule: touch_input.c)
        nativeDevices(touch, controller);
        updateShowing();
    }

    @Override
    public void onInputDeviceAdded(int deviceId) {
        refreshInputDevices();
    }

    @Override
    public void onInputDeviceRemoved(int deviceId) {
        refreshInputDevices();
    }

    @Override
    public void onInputDeviceChanged(int deviceId) {
        refreshInputDevices();
    }

    private void setHidden(boolean hidden) {
        reset();
        this.hidden = hidden;
        preferences.edit().putBoolean(KEY_HIDDEN, hidden).apply();
        updateSensors();
    }

    // ---------- the controller state the game reads

    /** lets go of every finger and sends a released controller */
    public void reset() {
        dragPointer = -1;
        dragControl = -1;
        owners.clear();
        buttonTouches.clear();
        toolbarFingers.clear();
        lookPointer = -1;
        stickFloating = false;
        crouchedKind = -1;
        gyroAim.reset();
        nativeLookReset();
        Arrays.fill(axes, 0);
        publish();
        invalidate();
    }

    private void publish() {
        if (editing || openDialogs > 0) {
            nativeState(0, 0, 0, 0, 0, 0, 0);
            return;
        }
        int bits = 0;
        axes[4] = 0;
        axes[5] = 0;
        for (int i = 0; i < owners.size(); i++) {
            int control = owners.valueAt(i);
            if (control < 0 || control >= layout.size() || (crouchToggles() && crouches(layout.type(control))))
                continue;
            bits = press(KINDS[layout.type(control)], bits);
        }
        // (crouch: tapped on, held by no finger)
        if (crouchedKind >= 0)
            crouchedKind = TouchContext.crouchKept(crouchedKind, gameControl(crouchedKind), context(), crouchToggles());
        if (crouchedKind >= 0)
            bits = press(KINDS[crouchedKind], bits);
        nativeState(axes[0], axes[1], axes[2], axes[3], axes[4], axes[5], bits);
    }

    /** a kind of control pressed: its trigger pulled (axes), and the buttons with its bit */
    private int press(Kind kind, int bits) {
        if (kind.trigger >= 0)
            axes[kind.trigger] = 32767;
        return kind.bit >= 0 ? bits | 1 << kind.bit : bits;
    }

    /** input.touch_crouch: whether Crouch is tapped on and off, not held */
    private boolean crouchToggles() {
        return (scene & SCENE_CROUCH_TOGGLE) != 0;
    }

    /** whether a kind of control crouches (is bound to Crouch) */
    private boolean crouches(int type) {
        return gameControl(type) == TouchContext.CONTROL_CROUCH;
    }

    /** held by a finger, or a crouch tapped on */
    private boolean pressed(int control) {
        return held(control) || (crouchedKind >= 0 && layout.type(control) == crouchedKind);
    }

    private boolean held(int control) {
        return owners.indexOfValue(control) >= 0;
    }

    /** the controls held, a bit each (TouchLayout.MAX_CONTROLS is 64) */
    private long heldControls() {
        long held = 0;
        for (int i = 0; i < owners.size(); i++) {
            int control = owners.valueAt(i);
            if (control >= 0 && control < 64)
                held |= 1L << control;
        }
        return held;
    }

    private float stickCenterX() {
        return stickFloating ? stickX : layout.x(TouchLayout.LEFT);
    }

    private float stickCenterY() {
        return stickFloating ? stickY : layout.y(TouchLayout.LEFT);
    }

    /** the floating stick, centred where a thumb landed (at rest there, even near an edge) */
    private void floatStick(float x, float y) {
        stickFloating = true;
        stickX = x;
        stickY = y;
    }

    private boolean inFloatingZone(float x, float y) {
        return x < logicalWidth * FLOATING_ZONE_WIDTH && y > logicalHeight - FLOATING_ZONE_HEIGHT;
    }

    private void moveStick(int control, float x, float y) {
        float dx = (x - stickCenterX()) / layout.radius(control);
        float dy = (y - stickCenterY()) / layout.radius(control);
        float length = (float) Math.sqrt(dx * dx + dy * dy);
        if (length < STICK_DEAD_ZONE) {
            dx = 0;
            dy = 0;
        } else if (length > 1) {
            dx /= length;
            dy /= length;
        }
        axes[0] = Math.round(dx * 32767);
        axes[1] = Math.round(dy * 32767);
    }

    // ---------- fingers

    private static boolean inside(float x, float y, float cx, float cy, float radius) {
        return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= radius * radius;
    }

    /** what is under a point (logical units): a toolbar item, a control, or NOTHING */
    private int hit(float x, float y) {
        if (inside(x, y, optionsX(), toolbarY(), toolbarRadius(OPTIONS_RADIUS)))
            return TOOLBAR_OPTIONS;
        if (editing && inside(x, y, optionsX() - 156, toolbarY(), toolbarRadius(OPTIONS_RADIUS)))
            return TOOLBAR_EXPORT;
        if (editing && inside(x, y, optionsX() - 78, toolbarY(), toolbarRadius(OPTIONS_RADIUS)))
            return TOOLBAR_IMPORT;
        if (!editing && inside(x, y, hideX(), toolbarY(), toolbarRadius(TOOLBAR_RADIUS)))
            return TOOLBAR_TOGGLE;
        if (hidden && !editing)
            return NOTHING;
        for (int i = 0; i < layout.size(); i++) {
            if (layout.shown(i) && layout.type(i) != TouchLayout.LEFT && (editing || !hiddenNow(layout.type(i)))
                    && inside(x, y, layout.x(i), layout.y(i), layout.radius(i)))
                return i;
        }
        int stick = TouchLayout.LEFT;
        float reach = editing ? 1 : STICK_REACH;
        if (layout.shown(stick) && !held(stick)
                && inside(x, y, layout.x(stick), layout.y(stick), layout.radius(stick) * reach))
            return stick;
        return NOTHING;
    }

    /**
     * whether a finger went down where Android keeps its edge gestures; in
     * sticky full screen the first swipe in from an edge only shows the
     * system bars, and Android also hands it over as an ordinary finger.
     * Only fingers on no control are kept out of them: the zones stay their
     * full size while the bars are hidden (all of the hidden 3-button bar),
     * and a control drawn there would never press
     */
    private boolean inGestureZone(float x, float y) {
        int[] insets = ((HaloActivity) getContext()).getSystemGestureInsetsPixels();
        return x < insets[0] || x >= getWidth() - insets[2]
            || y < insets[1] || y >= getHeight() - insets[3];
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        int action = event.getActionMasked();
        lastTouch = SystemClock.uptimeMillis();
        if (idle < 1f)
            fadeWhenIdle();
        if (!showing && !editing && openDialogs == 0) {
            // the menus and cinematics take the fingers (SDL's surface below)
            if (action == MotionEvent.ACTION_DOWN)
                return false;
            if (owners.size() > 0 || lookPointer >= 0)
                reset();
            return true;
        }
        // (most of a game's touches only aim, and change nothing drawn)
        boolean wasEditing = editing;
        boolean wasHidden = hidden;
        long wasHeld = heldControls();
        int wasStickX = axes[0];
        int wasStickY = axes[1];
        float wasCenterX = stickCenterX();
        float wasCenterY = stickCenterY();
        if (editing)
            editTouch(event);
        else
            gameTouch(event);
        publish();
        if (editing || wasEditing || hidden != wasHidden || heldControls() != wasHeld || axes[0] != wasStickX
                || axes[1] != wasStickY || stickCenterX() != wasCenterX || stickCenterY() != wasCenterY)
            invalidate();
        return true;
    }

    @Override
    public boolean performClick() {
        super.performClick();
        return true;
    }

    private void gameTouch(MotionEvent event) {
        int index = event.getActionIndex();
        int id = event.getPointerId(index);
        switch (event.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN:
                fingerDown(id, event.getX(index), event.getY(index));
                break;
            case MotionEvent.ACTION_MOVE:
                for (int i = 0; i < event.getPointerCount(); i++)
                    fingerMoved(event.getPointerId(i), event.getX(i), event.getY(i));
                break;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP:
                fingerUp(id, event.getX(index), event.getY(index));
                break;
            case MotionEvent.ACTION_CANCEL:
                reset();
                break;
            default:
                break;
        }
    }

    /** a finger down in a game, at a point in pixels */
    private void fingerDown(int id, float px, float py) {
        float x = logical(px);
        float y = logical(py);
        int target = hit(x, y);
        int stick = TouchLayout.LEFT;
        if (target == NOTHING && layout.floatingStick && !hidden && layout.shown(stick) && !held(stick)
                && inFloatingZone(x, y) && !inGestureZone(px, py)) {
            floatStick(x, y);
            target = stick;
        }
        if (target == TOOLBAR_OPTIONS || target == TOOLBAR_TOGGLE) {
            toolbarFingers.put(id, new float[] {px, py, target});
        } else if (target != NOTHING) {
            owners.put(id, target);
            if (layout.type(target) == TouchLayout.LEFT) {
                moveStick(target, x, y);
            } else {
                buttonTouches.put(id, new float[] {px, py});
                int type = layout.type(target);
                crouchedKind = TouchContext.crouchPressed(crouchedKind, type, gameControl(type), crouchToggles());
                tap();
            }
        } else if (!hidden && lookPointer < 0 && !inGestureZone(px, py)) {
            startLook(id, px, py);
        }
    }

    private void startLook(int id, float px, float py) {
        lookPointer = id;
        lookX = px;
        lookY = py;
        if (owners.get(id, NOTHING) == NOTHING)
            owners.put(id, LOOK);
    }

    private boolean movedPastSlop(float[] origin, float px, float py) {
        return Math.hypot(px - origin[0], py - origin[1]) > SLOP * scale;
    }

    private void fingerMoved(int id, float px, float py) {
        int control = owners.get(id, NOTHING);
        if (control >= 0 && layout.type(control) == TouchLayout.LEFT)
            moveStick(control, logical(px), logical(py));
        float[] toolbar = toolbarFingers.get(id);
        if (toolbar != null && movedPastSlop(toolbar, px, py)) {
            // a swipe that began on the toolbar aims; the item does not act
            toolbarFingers.remove(id);
            if (!hidden && lookPointer < 0)
                startLook(id, toolbar[0], toolbar[1]);
        }
        float[] origin = buttonTouches.get(id);
        if (origin != null && lookPointer >= 0 && lookPointer != id) {
            // while another finger aims, a held button's finger may only take over from where it is
            origin[0] = px;
            origin[1] = py;
        }
        if (lookPointer < 0 && origin != null && movedPastSlop(origin, px, py)) {
            // a held button (either Fire too) also aims
            lookPointer = id;
            lookX = origin[0];
            lookY = origin[1];
        }
        if (id == lookPointer) {
            nativeLook(logical(px - lookX) * sensitivity, logical(py - lookY) * sensitivity);
            lookX = px;
            lookY = py;
        }
    }

    private void fingerUp(int id, float px, float py) {
        float[] toolbar = toolbarFingers.get(id);
        if (toolbar != null) {
            toolbarFingers.remove(id);
            int item = (int) toolbar[2];
            if (hit(logical(px), logical(py)) == item)
                toolbarAction(item);
            return;
        }
        int control = owners.get(id, NOTHING);
        if (control >= 0 && layout.type(control) == TouchLayout.LEFT) {
            axes[0] = 0;
            axes[1] = 0;
            stickFloating = false;
        }
        if (id == lookPointer)
            lookPointer = -1;
        owners.delete(id);
        buttonTouches.remove(id);
    }

    private void toolbarAction(int item) {
        if (item == TOOLBAR_OPTIONS)
            showOptions();
        else if (item == TOOLBAR_TOGGLE)
            setHidden(!hidden);
        performClick();
    }

    /** a finger in the layout editor: drags controls; Save, Export and Import act at once */
    private void editTouch(MotionEvent event) {
        int action = event.getActionMasked();
        int index = event.getActionIndex();
        int id = event.getPointerId(index);
        if (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN) {
            if (dragPointer >= 0)
                return;
            float x = logical(event.getX(index));
            float y = logical(event.getY(index));
            int target = hit(x, y);
            if (target == TOOLBAR_OPTIONS) {
                if (saveLayout()) {
                    reset();
                    editing = false;
                    performClick();
                }
            } else if (target == TOOLBAR_EXPORT || target == TOOLBAR_IMPORT) {
                reset();
                ((HaloActivity) getContext()).chooseLayoutFile(target == TOOLBAR_EXPORT, exportLayout());
            } else if (target >= 0) {
                dragPointer = id;
                dragControl = target;
                dragOffsetX = x - layout.x(target);
                dragOffsetY = y - layout.y(target);
            }
        } else if (action == MotionEvent.ACTION_MOVE && dragPointer >= 0) {
            int pointer = event.findPointerIndex(dragPointer);
            if (pointer >= 0) {
                layout.move(dragControl, logical(event.getX(pointer)) - dragOffsetX,
                    logical(event.getY(pointer)) - dragOffsetY);
            }
        } else if (action == MotionEvent.ACTION_CANCEL
                || ((action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP) && id == dragPointer)) {
            dragPointer = -1;
            dragControl = -1;
        }
    }

    // ---------- the buttons' names, from the player's profile

    private void readBindings() {
        int serial = nativeBindings(bindingsRead);
        if (serial == 0 || serial == bindingsSerial)
            return;
        bindingsSerial = serial;
        if (!Arrays.equals(bindingsRead, bindings)) {
            System.arraycopy(bindingsRead, 0, bindings, 0, bindings.length);
            Arrays.fill(labels, null);
            invalidate();
        }
    }

    /** a control's name: its button and what the profile has it do ("A / Jump", "Fire", "RT") */
    private String label(int type) {
        if (labels[type] == null)
            labels[type] = makeLabel(type);
        return labels[type];
    }

    private String makeLabel(int type) {
        Kind kind = KINDS[type];
        int control = gameControl(type);
        if (control < 0)
            return kind.buttonName;
        return kind.faceButton ? kind.buttonName + " / " + GAME_CONTROLS[control] : GAME_CONTROLS[control];
    }

    /** the game control a kind's button does in the player's profile, or -1 for none */
    private int gameControl(int type) {
        Kind kind = KINDS[type];
        if (kind.gamepadButton < 0)
            return -1;
        int control = bindings[kind.gamepadButton];
        return control >= 0 && control < GAME_CONTROLS.length ? control : -1;
    }

    /** player 1's state, which the game sends with the scene (TouchContext) */
    private int context() {
        return TouchContext.fromScene(scene);
    }

    /** a button the state hides: jump and crouch in a vehicle's seat */
    private boolean hiddenNow(int type) {
        return TouchContext.hidden(gameControl(type), context());
    }

    /** a light tap of the phone's vibrator at a button's press, with the vibration on */
    private void tap() {
        if (vibrator == null || !layout.rumbleEnabled || !vibrator.hasVibrator())
            return;
        try {
            VibrationEffect effect = Build.VERSION.SDK_INT >= 29
                ? VibrationEffect.createPredefined(VibrationEffect.EFFECT_TICK)
                : VibrationEffect.createOneShot(12, 60);
            vibrator.vibrate(effect, rumbleAttributes);
        } catch (RuntimeException ignored) {
            // the vibrator may already be gone with the activity
        }
    }

    /** an art's glow: its shape, blurred, in the HUD's colour, on a margin of GLOW_MARGIN */
    private Bitmap glow(Bitmap art) {
        Bitmap glow = glows.get(art);
        if (glow != null)
            return glow;
        int margin = Math.round(Math.max(art.getWidth(), art.getHeight()) * GLOW_MARGIN);
        Bitmap shape = art.extractAlpha();
        glow = Bitmap.createBitmap(art.getWidth() + 2 * margin, art.getHeight() + 2 * margin, Bitmap.Config.ARGB_8888);
        Paint blur = new Paint(Paint.ANTI_ALIAS_FLAG);
        blur.setColor(HUD_COLOR);
        blur.setMaskFilter(new BlurMaskFilter(margin * 0.5f, BlurMaskFilter.Blur.NORMAL));
        // (the shape a little larger, so that the glow reaches past the art's edge)
        float grow = GLOW_GROW / 2;
        new Canvas(glow).drawBitmap(shape, null, new RectF(margin - art.getWidth() * grow, margin - art.getHeight() * grow,
            margin + art.getWidth() * (1 + grow), margin + art.getHeight() * (1 + grow)), blur);
        shape.recycle();
        glow.setHasMipMap(true);
        glows.put(art, glow);
        return glow;
    }

    /** the controller buttons' art: display.hud_button_style, which the game sends with the scene */
    private Bitmap art(int type) {
        int art = KIND_ART[type];
        if (art < 0)
            return null;
        if (art >= 6)
            return controlArt[art - 6];
        int style = (scene >> SCENE_STYLE_SHIFT) & 3;
        return faces[style < faces.length ? style : 0][art];
    }

    private String controlName(int type) {
        return type == TouchLayout.LEFT ? "Move stick" : label(type);
    }

    // ---------- the layout, saved and in files

    private boolean saveLayout() {
        // (the settings saved with every layout are the same in both)
        (layout == landscapeLayout ? portraitLayout : landscapeLayout).takeSettings(layout);
        boolean saved = preferences.edit()
            .putString(KEY_CONFIGURATION, landscapeLayout.exportConfiguration(sensitivity))
            .putString(KEY_PORTRAIT_CONFIGURATION, portraitLayout.exportConfiguration(sensitivity))
            .commit();
        if (!saved)
            Toast.makeText(getContext(), "The touch layout could not be saved. Try again.", Toast.LENGTH_LONG).show();
        return saved;
    }

    public String exportLayout() {
        return layout.exportConfiguration(sensitivity);
    }

    /**
     * replaces the layout of the file's orientation (a portrait layout's
     * file says so) with the file's, and its settings both layouts' (saved
     * first: it throws if the file is not valid)
     */
    public void importLayout(String text) {
        TouchLayout.Configuration imported = TouchLayout.importConfiguration(text);
        boolean inUse = imported.layout.portrait == layout.portrait;
        if (inUse) {
            imported.layout.bounds(logicalWidth, logicalHeight);
            imported.layout.setMinimumRadius(minimumRadius());
        }
        TouchLayout other = imported.layout.portrait ? landscapeLayout : portraitLayout;
        other.takeSettings(imported.layout);
        String normalized = imported.layout.exportConfiguration(imported.sensitivity);
        if (!preferences.edit()
                .putString(imported.layout.portrait ? KEY_PORTRAIT_CONFIGURATION : KEY_CONFIGURATION, normalized)
                .putString(imported.layout.portrait ? KEY_CONFIGURATION : KEY_PORTRAIT_CONFIGURATION,
                    other.exportConfiguration(imported.sensitivity))
                .commit())
            throw new IllegalArgumentException("Could not save the imported layout");
        reset();
        if (imported.layout.portrait)
            portraitLayout = imported.layout;
        else
            landscapeLayout = imported.layout;
        if (inUse)
            layout = imported.layout;
        else
            layout.takeSettings(imported.layout);
        sensitivity = imported.sensitivity;
        setHidden(false);
        cancelRumble();
        // (laid out again: the new layout has no top inset, picture or
        // gather of its own until it is)
        layoutControls();
        invalidate();
    }

    // ---------- the options

    /** opens an option dialog; the controls are released while any is open, and saved when the last closes */
    private void openDialog(AlertDialog dialog) {
        dialog.setOnDismissListener(d -> {
            openDialogs--;
            if (openDialogs > 0)
                return;
            openDialogs = 0;
            saveLayout();
            reset();
            updateSensors();
        });
        openDialogs++;
        reset();
        dialog.show();
    }

    private void showOptions() {
        String[] items = {"General", "Edit buttons layout", "Look sensitivity"};
        AlertDialog dialog = new AlertDialog.Builder(getContext())
            .setTitle("Options")
            .setItems(items, (d, which) -> {
                if (which == 0) {
                    post(this::showGeneral);
                } else if (which == 1) {
                    editing = true;
                    setHidden(false);
                } else {
                    post(this::showSensitivity);
                }
            })
            .setNegativeButton("Close", null)
            .create();
        openDialog(dialog);
    }

    private TextView text(String value) {
        TextView view = new TextView(getContext());
        view.setText(value);
        return view;
    }

    private Button button(String label, View.OnClickListener listener) {
        Button button = new Button(getContext());
        button.setText(label);
        button.setOnClickListener(listener);
        return button;
    }

    private void showGeneral() {
        cancelRumble();
        LinearLayout panel = new LinearLayout(getContext());
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setPadding(24, 16, 24, 16);

        boolean hasRumble = vibrator != null && vibrator.hasVibrator();
        Switch rumble = new Switch(getContext());
        rumble.setText(hasRumble ? "Vibration" : "Vibration (not on this phone)");
        rumble.setChecked(layout.rumbleEnabled);
        rumble.setEnabled(hasRumble);
        rumble.setOnCheckedChangeListener((view, enabled) -> {
            layout.rumbleEnabled = enabled;
            cancelRumble();
            saveLayout();
        });
        panel.addView(rumble);

        Switch gyro = new Switch(getContext());
        gyro.setText(gyroscope != null ? "Gyroscope aiming (experimental)" : "Gyroscope aiming (not on this phone)");
        gyro.setChecked(layout.gyroscopeEnabled);
        gyro.setEnabled(gyroscope != null);
        gyro.setOnCheckedChangeListener((view, enabled) -> {
            layout.gyroscopeEnabled = enabled;
            gyroAim.reset();
            saveLayout();
        });
        panel.addView(gyro);

        Switch floating = new Switch(getContext());
        floating.setText("Floating move stick: it goes where your thumb lands in the lower left");
        floating.setChecked(layout.floatingStick);
        floating.setOnCheckedChangeListener((view, enabled) -> {
            layout.floatingStick = enabled;
            saveLayout();
        });
        panel.addView(floating);

        TextView opacityLabel = text("");
        SeekBar opacity = new SeekBar(getContext());
        int steps = Math.round((TouchLayout.MAX_OPACITY - TouchLayout.MIN_OPACITY) * 20);
        opacity.setMax(steps);
        opacity.setProgress(Math.round((layout.opacity - TouchLayout.MIN_OPACITY) * 20));
        Runnable showOpacity = () -> opacityLabel.setText(String.format(Locale.US, "Opacity: %d%%",
            Math.round(layout.opacity * 100)));
        showOpacity.run();
        opacity.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar bar, int progress, boolean user) {
                layout.setOpacity(Math.min(TouchLayout.MAX_OPACITY, TouchLayout.MIN_OPACITY + progress / 20f));
                showOpacity.run();
                invalidate();
            }

            @Override
            public void onStartTrackingTouch(SeekBar bar) {
            }

            @Override
            public void onStopTrackingTouch(SeekBar bar) {
                saveLayout();
            }
        });
        panel.addView(opacityLabel);
        panel.addView(opacity);

        // (scrolled: in portrait, or on a short display, the rows do not all fit)
        ScrollView scroll = new ScrollView(getContext());
        scroll.addView(panel);
        AlertDialog dialog = new AlertDialog.Builder(getContext())
            .setTitle("General")
            .setView(scroll)
            .setPositiveButton("Done", null)
            .create();
        panel.addView(button("Hide or add buttons", view -> {
            dialog.dismiss();
            post(this::showButtonManager);
        }));
        panel.addView(button("Edit buttons size", view -> {
            dialog.dismiss();
            post(this::showButtonSizes);
        }));
        openDialog(dialog);
    }

    private String rowName(int control, boolean withHidden) {
        String name = controlName(layout.type(control));
        if (control >= TouchLayout.BASE_COUNT)
            name += " (copy)";
        if (withHidden && !layout.shown(control))
            name += " (hidden)";
        return name;
    }

    private LinearLayout row(int control, boolean withHidden) {
        LinearLayout row = new LinearLayout(getContext());
        row.setPadding(16, 4, 16, 4);
        TextView name = text(rowName(control, withHidden));
        name.setTextColor(Color.WHITE);
        row.addView(name, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));
        return row;
    }

    private void showButtonSizes() {
        LinearLayout list = new LinearLayout(getContext());
        list.setOrientation(LinearLayout.VERTICAL);
        for (int i = 0; i < layout.size(); i++) {
            final int control = i;
            LinearLayout row = row(control, true);
            TextView value = text("");
            value.setTextColor(Color.WHITE);
            Button smaller = new Button(getContext());
            Button larger = new Button(getContext());
            Runnable refresh = () -> {
                int percent = Math.round(layout.sizeScale(control) * 100);
                value.setText(percent + "%");
                smaller.setEnabled(percent > Math.round(TouchLayout.MIN_SIZE * 100));
                larger.setEnabled(percent < Math.round(TouchLayout.MAX_SIZE * 100));
            };
            smaller.setText("-");
            smaller.setOnClickListener(view -> resize(control, -10, refresh));
            larger.setText("+");
            larger.setOnClickListener(view -> resize(control, 10, refresh));
            row.addView(smaller);
            row.addView(value);
            row.addView(larger);
            list.addView(row);
            refresh.run();
        }
        ScrollView scroll = new ScrollView(getContext());
        scroll.addView(list);
        openDialog(new AlertDialog.Builder(getContext())
            .setTitle("Edit buttons size")
            .setView(scroll)
            .setPositiveButton("Done", null)
            .create());
    }

    private void resize(int control, int percent, Runnable refresh) {
        int size = Math.round(layout.sizeScale(control) * 100) + percent;
        size = Math.max(Math.round(TouchLayout.MIN_SIZE * 100), Math.min(Math.round(TouchLayout.MAX_SIZE * 100), size));
        layout.setSize(control, size / 100f);
        saveLayout();
        refresh.run();
        invalidate();
    }

    private void showButtonManager() {
        LinearLayout list = new LinearLayout(getContext());
        list.setOrientation(LinearLayout.VERTICAL);
        Runnable[] refresh = new Runnable[1];
        refresh[0] = () -> {
            list.removeAllViews();
            for (int i = 0; i < layout.size(); i++) {
                final int control = i;
                LinearLayout row = row(control, false);
                row.addView(button(layout.shown(control) ? "Hide" : "Show", view -> {
                    reset();
                    layout.setShown(control, !layout.shown(control));
                    saveLayout();
                    refresh[0].run();
                    invalidate();
                }));
                if (layout.type(control) != TouchLayout.LEFT) {
                    row.addView(button("Duplicate", view -> {
                        reset();
                        if (layout.duplicate(control) < 0)
                            toast("At most " + TouchLayout.MAX_CONTROLS + " controls. Reset removes the copies.");
                        else
                            saveLayout();
                        refresh[0].run();
                        invalidate();
                    }));
                }
                list.addView(row);
            }
        };
        refresh[0].run();
        ScrollView scroll = new ScrollView(getContext());
        scroll.addView(list);
        AlertDialog dialog = new AlertDialog.Builder(getContext())
            .setTitle("Hide or add buttons")
            .setView(scroll)
            .setPositiveButton("Done", null)
            .setNegativeButton("Reset", null)
            .setNeutralButton("Add button", null)
            .create();
        openDialog(dialog);
        dialog.getButton(AlertDialog.BUTTON_NEGATIVE).setOnClickListener(view -> confirmReset(refresh[0]));
        dialog.getButton(AlertDialog.BUTTON_NEUTRAL).setOnClickListener(view -> chooseButtonToAdd(refresh[0]));
    }

    private void confirmReset(Runnable refresh) {
        new AlertDialog.Builder(getContext())
            .setTitle("Reset the buttons?")
            .setMessage("This puts every button back in its place and size, shows them all and removes the copies.")
            .setNegativeButton("Cancel", null)
            .setPositiveButton("Reset", (d, which) -> {
                reset();
                layout.resetDefaults();
                setHidden(false);
                saveLayout();
                refresh.run();
                invalidate();
            })
            .show();
    }

    private void chooseButtonToAdd(Runnable refresh) {
        String[] names = new String[TouchLayout.BASE_COUNT];
        for (int i = 0; i < names.length; i++)
            names[i] = controlName(i);
        new AlertDialog.Builder(getContext())
            .setTitle("Add button")
            .setItems(names, (d, type) -> {
                reset();
                if (layout.add(type) < 0)
                    toast(type == TouchLayout.LEFT ? "The move stick already shows." : "At most " + TouchLayout.MAX_CONTROLS + " controls.");
                else
                    saveLayout();
                refresh.run();
                invalidate();
            })
            .setNegativeButton("Cancel", null)
            .show();
    }

    private void showSensitivity() {
        LinearLayout panel = new LinearLayout(getContext());
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setPadding(32, 16, 32, 16);
        TextView value = text("");
        SeekBar slider = new SeekBar(getContext());
        int steps = Math.round((TouchLayout.MAX_SENSITIVITY - TouchLayout.MIN_SENSITIVITY) / 0.025f);
        slider.setMax(steps);
        slider.setProgress(Math.round((sensitivity - TouchLayout.MIN_SENSITIVITY) / 0.025f));
        Runnable showValue = () -> value.setText(String.format(Locale.US, "Look sensitivity: %.2fx", sensitivity));
        showValue.run();
        slider.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar bar, int progress, boolean user) {
                sensitivity = Math.min(TouchLayout.MAX_SENSITIVITY, TouchLayout.MIN_SENSITIVITY + progress * 0.025f);
                showValue.run();
            }

            @Override
            public void onStartTrackingTouch(SeekBar bar) {
            }

            @Override
            public void onStopTrackingTouch(SeekBar bar) {
                saveLayout();
            }
        });
        panel.addView(value);
        panel.addView(slider);
        openDialog(new AlertDialog.Builder(getContext())
            .setTitle("Look sensitivity")
            .setView(panel)
            .setPositiveButton("Done", null)
            .create());
    }

    private void toast(String message) {
        Toast.makeText(getContext(), message, Toast.LENGTH_LONG).show();
    }

    // ---------- the gyroscope and the vibration

    private void updateSensors() {
        boolean needed = deviceInputActive && showing && !hidden && layout.gyroscopeEnabled && gyroscope != null;
        if (needed && !gyroRegistered) {
            gyroAim.reset();
            gyroRegistered = sensors.registerListener(this, gyroscope, SensorManager.SENSOR_DELAY_GAME);
        } else if (!needed && gyroRegistered) {
            sensors.unregisterListener(this);
            gyroRegistered = false;
            gyroAim.reset();
        }
    }

    @Override
    public void onSensorChanged(SensorEvent event) {
        if (!deviceInputActive || !showing || hidden || !layout.gyroscopeEnabled || editing || openDialogs > 0) {
            gyroAim.reset();
            return;
        }
        int rotation = ((WindowManager) getContext().getSystemService(Context.WINDOW_SERVICE))
            .getDefaultDisplay().getRotation();
        if (gyroAim.sample(event.timestamp, event.values[0], event.values[1], rotation, gyroDelta)) {
            // as a swipe of this many logical units (which the profile's invert leaves alone)
            nativeGyro(-gyroDelta[0] / LOOK_RADIANS_PER_UNIT * sensitivity,
                -gyroDelta[1] / LOOK_RADIANS_PER_UNIT * sensitivity);
        }
    }

    @Override
    public void onAccuracyChanged(Sensor sensor, int accuracy) {
    }

    /** the phone vibrates as port 0's controller would, while the controls show */
    private void updateRumble() {
        if (vibrator == null || !vibrator.hasVibrator())
            return;
        boolean active = showing && !hidden && layout.rumbleEnabled && !editing && openDialogs == 0;
        int amplitude = active ? nativeRumble() : 0;
        if (amplitude == 0) {
            cancelRumble();
            return;
        }
        long now = SystemClock.uptimeMillis();
        if (amplitude == lastAmplitude && now - lastVibration < VIBRATION_RENEW_MS)
            return;
        try {
            int strength = vibrator.hasAmplitudeControl() ? amplitude : VibrationEffect.DEFAULT_AMPLITUDE;
            vibrator.vibrate(VibrationEffect.createOneShot(VIBRATION_MS, strength), rumbleAttributes);
            lastAmplitude = amplitude;
            lastVibration = now;
        } catch (RuntimeException e) {
            cancelRumble();
        }
    }

    private void cancelRumble() {
        if (lastAmplitude != 0 && vibrator != null) {
            try {
                vibrator.cancel();
            } catch (RuntimeException ignored) {
                // the vibrator may already be gone with the activity
            }
        }
        lastAmplitude = 0;
    }

    // ---------- drawing

    private static int alpha(int color, float opacity) {
        int alpha = Math.round(Color.alpha(color) * opacity);
        return (color & 0x00ffffff) | (Math.max(0, Math.min(255, alpha)) << 24);
    }

    /**
     * the idle fade: toward IDLE_OPACITY once no finger has touched the
     * screen for IDLE_MS, back to whole at a touch; smooth, a step a frame
     */
    private void fadeWhenIdle() {
        long now = SystemClock.uptimeMillis();
        long elapsed = Math.min(now - lastFade, 100);
        lastFade = now;
        boolean touched = owners.size() > 0 || lookPointer >= 0 || toolbarFingers.size() > 0
            || now - lastTouch < IDLE_MS;
        float target = touched ? 1f : IDLE_OPACITY;
        float step = (1f - IDLE_OPACITY) * elapsed / (touched ? FADE_IN_MS : FADE_OUT_MS);
        float faded = idle < target ? Math.min(target, idle + step) : Math.max(target, idle - step);
        if (faded != idle) {
            idle = faded;
            invalidate();
        }
    }

    private void label(Canvas canvas, float x, float y, float radius, String label, float textSize, float opacity) {
        paint.setStyle(Paint.Style.FILL);
        paint.setColor(alpha(HUD_COLOR, Math.max(opacity, 0.5f)));
        paint.setTextAlign(Paint.Align.CENTER);
        paint.setShadowLayer(2, 0, 1, alpha(Color.BLACK, 0.6f * opacity));
        textSize = fittedTextSize(label, radius, textSize);
        paint.setTextSize(textSize);
        canvas.drawText(label, x, y + textSize * 0.36f, paint);
        paint.clearShadowLayer();
    }

    /** a picture, its longer side 2 * radius, centred on a point; brighter while held */
    private void fitted(Canvas canvas, Bitmap bitmap, float x, float y, float radius, float alpha, boolean active) {
        float scale = radius / Math.max(bitmap.getWidth(), bitmap.getHeight());
        float halfWidth = bitmap.getWidth() * scale;
        float halfHeight = bitmap.getHeight() * scale;
        pictures.setAlpha(Math.max(0, Math.min(255, Math.round(alpha * 255))));
        pictures.setColorFilter(active ? pressed : null);
        pictureBounds.set(x - halfWidth, y - halfHeight, x + halfWidth, y + halfHeight);
        canvas.drawBitmap(bitmap, null, pictureBounds, pictures);
        pictures.setColorFilter(null);
    }

    /**
     * a control, with nothing drawn behind it: a controller button drawn as
     * the HUD draws it (A, B, X, Y, black, white, the triggers and the
     * sticks), with its caption under it (input.touch_captions: what it does
     * in the player's profile), or start, back and the D-pad as line icons.
     * Each takes a finger anywhere in its circle; held, it is drawn brighter
     * and a little smaller.
     */
    private void control(Canvas canvas, int control, boolean active, float opacity) {
        int type = layout.type(control);
        float x = layout.x(control);
        float y = layout.y(control);
        float radius = layout.radius(control);
        boolean minor = MINOR_KINDS[type];
        float sink = active ? PRESSED_SIZE : 1f;
        Bitmap art = art(type);
        if (art != null) {
            int game = gameControl(type);
            int context = editing ? 0 : context();
            long now = SystemClock.uptimeMillis();
            float glow = TouchContext.glow(game, context, now);
            float size = radius * ART_SIZE * sink * TouchContext.swell(game, context, now);
            if (glow > 0)
                fitted(canvas, glow(art), x, y, size * (1 + 2 * GLOW_MARGIN), glow * Math.max(opacity, 0.5f), false);
            fitted(canvas, art, x, y, size, (active ? 1f : minor ? MINOR_BUTTON_ALPHA : BUTTON_ALPHA) * opacity
                * TouchContext.alpha(game, context), active || glow > 0.6f);
            // (under the art, as drawn when it is not held, so that it stays put)
            caption(canvas, type, x, y + radius * ART_SIZE * art.getHeight()
                / Math.max(art.getWidth(), art.getHeight()), opacity * TouchContext.alpha(game, context));
            return;
        }
        Kind kind = KINDS[type];
        Bitmap icon = kind.arrow >= 0 ? arrowIcon : kind.bit == 6 ? pauseIcon : kind.bit == 4 ? backIcon : null;
        float iconOpacity = (active ? 1f : minor ? MINOR_ICON_ALPHA : ICON_ALPHA) * Math.max(opacity, 0.5f);
        if (icon == null) {
            float textSize = 11 * Math.max(layout.sizeScale(control), radius / 36f);
            label(canvas, x, y, radius, label(type), Math.min(textSize, 16), iconOpacity);
            return;
        }
        float size = radius * (minor ? MINOR_ICON_SIZE : ICON_SIZE) * sink;
        canvas.save();
        if (kind.arrow > 0)
            canvas.rotate(kind.arrow, x, y);
        fitted(canvas, icon, x, y, size, iconOpacity, active);
        canvas.restore();
    }

    /** input.touch_captions: the game control a button does, a short word in the HUD's font and colour, under it */
    private void caption(Canvas canvas, int type, float x, float bottom, float opacity) {
        int game = gameControl(type);
        if ((scene & SCENE_CAPTIONS) == 0 || game < 0)
            return;
        paint.setStyle(Paint.Style.FILL);
        paint.setTextAlign(Paint.Align.CENTER);
        paint.setTypeface(captionFont);
        paint.setTextSize(CAPTION_SIZE);
        paint.setLetterSpacing(0.06f);
        paint.setShadowLayer(2.5f, 0, 1, alpha(Color.BLACK, 0.8f * Math.max(opacity, 0.5f)));
        paint.setColor(alpha(HUD_COLOR, CAPTION_ALPHA * Math.max(opacity, 0.5f)));
        canvas.drawText(TouchContext.caption(game, editing ? 0 : context(), CAPTIONS[game]), x,
            bottom + CAPTION_SIZE + 1, paint);
        paint.clearShadowLayer();
        paint.setLetterSpacing(0);
        paint.setTypeface(null);
    }

    /** a long name, "Y / Weapon", is made smaller to fit inside the circle */
    private float fittedTextSize(String label, float radius, float textSize) {
        LabelFit fit = labelFits.get(labelFitKey.set(label, radius, textSize));
        if (fit != null)
            return fit.fitted;
        paint.setTextSize(textSize);
        float fitted = textSize;
        float width = paint.measureText(label);
        if (width > radius * LABEL_WIDTH)
            fitted *= radius * LABEL_WIDTH / width;
        if (labelFits.size() >= LABEL_FITS)
            labelFits.clear();
        fit = new LabelFit().set(label, radius, textSize);
        fit.fitted = fitted;
        labelFits.put(fit, fit);
        return fitted;
    }

    /** a toolbar item, a line icon or (the editor's) a word: never fainter than half, so that it can always be found */
    private void toolbarItem(Canvas canvas, float x, float radius, Bitmap icon, String label) {
        float opacity = Math.max(layout.opacity * idle, 0.5f);
        if (icon != null)
            fitted(canvas, icon, x, toolbarY(), toolbarRadius(radius) * MINOR_ICON_SIZE, ICON_ALPHA * opacity, false);
        else
            label(canvas, x, toolbarY(), toolbarRadius(radius), label, 13, opacity);
    }

    private void drawToolbar(Canvas canvas) {
        if (editing) {
            toolbarItem(canvas, optionsX(), OPTIONS_RADIUS, null, "Save");
            toolbarItem(canvas, optionsX() - 156, OPTIONS_RADIUS, null, "Export");
            toolbarItem(canvas, optionsX() - 78, OPTIONS_RADIUS, null, "Import");
            label(canvas, logicalWidth / 2, toolbarY() + 58, 1000, "Drag the controls, then push Save", 14, 1f);
        } else {
            toolbarItem(canvas, hideX(), TOOLBAR_RADIUS, hidden ? showIcon : hideIcon, null);
            toolbarItem(canvas, optionsX(), OPTIONS_RADIUS, optionsIcon, null);
        }
    }

    /** the move stick: a faint ring, and a knob that brightens while held */
    private void drawStick(Canvas canvas, float opacity) {
        int stick = TouchLayout.LEFT;
        float radius = layout.radius(stick);
        float size = layout.sizeScale(stick);
        // (a floating stick near an edge is drawn whole, nearer the middle than its centre)
        float x = Math.max(radius, Math.min(logicalWidth - radius, stickCenterX()));
        float y = Math.max(radius, Math.min(logicalHeight - radius, stickCenterY()));
        float knob = Math.max(STICK_KNOB_RADIUS * size, radius * 0.375f);
        float knobX = x + axes[0] / 32767f * radius;
        float knobY = y + axes[1] / 32767f * radius;
        boolean active = held(stick) || dragControl == stick;
        paint.setShadowLayer(3, 0, 1, alpha(Color.BLACK, 0.5f * opacity));
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(2);
        paint.setColor(alpha(HUD_COLOR, STICK_RING_ALPHA * opacity));
        canvas.drawCircle(x, y, radius - 1, paint);
        paint.setStyle(Paint.Style.FILL);
        paint.setColor(alpha(HUD_COLOR, (active ? 0.8f : STICK_KNOB_ALPHA) * opacity));
        canvas.drawCircle(knobX, knobY, knob, paint);
        paint.clearShadowLayer();
    }

    /**
     * portrait's frame in display.frame_style's look, around the game's
     * picture and never over it (clipped out): the strip along the top, the
     * panel under the picture with its rings, and a line or brackets just
     * outside the picture's edges; in menus too
     */
    private void drawFrame(Canvas canvas) {
        int style = (scene >> SCENE_FRAME_SHIFT) & 3;
        if (pictureRect.isEmpty() || (scene & SCENE_KNOWN) == 0 || style == 0 || style > 2)
            return;
        if (frame == null || frameStyle != style) {
            frame = new Picture();
            frameStyle = style;
            recordFrame(frame.beginRecording((int) Math.ceil(logicalWidth), (int) Math.ceil(logicalHeight)), style);
            frame.endRecording();
        }
        canvas.save();
        canvas.clipOutRect(pictureRect);
        canvas.drawPicture(frame);
        canvas.restore();
    }

    /** the frame's drawing, in logical units: halo (the HUD's cyan on navy, rings like the menus') or xbox (green on ribbed black) */
    private void recordFrame(Canvas canvas, int style) {
        boolean halo = style == FRAME_HALO;
        int accent = halo ? HUD_COLOR : FRAME_GREEN;
        float width = logicalWidth, height = logicalHeight, strip = layout.stripBottom();
        float x = width / 2, y = pictureRect.bottom + (height - pictureRect.bottom) / 2;
        Paint ink = new Paint(Paint.ANTI_ALIAS_FLAG);
        if (halo) {
            ink.setShader(new LinearGradient(0, 0, 0, height, Color.rgb(5, 9, 17), Color.rgb(11, 19, 35),
                Shader.TileMode.CLAMP));
            canvas.drawRect(0, 0, width, height, ink);
            ink.setShader(null);
            ink.setColor(Color.argb(46, 0, 0, 0));
            for (float line = 0; line < height; line += 3)
                canvas.drawRect(0, line, width, line + 1, ink);
            ink.setStyle(Paint.Style.STROKE);
            ink.setStrokeWidth(0.75f);
            float[][] rings = {{150, 0.10f}, {205, 0.13f}, {212, 0.07f}, {300, 0.055f}};
            for (float[] ring : rings) {
                ink.setColor(alpha(accent, ring[1]));
                canvas.drawCircle(x, y, ring[0], ink);
            }
            ink.setColor(alpha(accent, 0.12f));
            for (int tick = 0; tick < 72; tick++) {
                double turn = tick * Math.PI / 36;
                float outer = tick % 6 == 0 ? 228 : 218;
                canvas.drawLine(x + 212 * (float) Math.cos(turn), y + 212 * (float) Math.sin(turn),
                    x + outer * (float) Math.cos(turn), y + outer * (float) Math.sin(turn), ink);
            }
        } else {
            canvas.drawColor(Color.rgb(7, 9, 7));
            ink.setStyle(Paint.Style.STROKE);
            ink.setStrokeWidth(1.5f);
            ink.setColor(Color.rgb(12, 18, 11));
            for (float line = -height; line < width; line += 6)
                canvas.drawLine(line, 0, line + height, height, ink);
            ink.setStyle(Paint.Style.FILL);
            ink.setShader(new RadialGradient(x, y, 230, alpha(accent, 0.27f), alpha(accent, 0), Shader.TileMode.CLAMP));
            canvas.drawCircle(x, y, 230, ink);
            ink.setShader(null);
            ink.setStyle(Paint.Style.STROKE);
            ink.setColor(alpha(accent, 0.24f));
            canvas.drawCircle(x, y, 120, ink);
            ink.setColor(alpha(accent, 0.1f));
            canvas.drawCircle(x, y, 126, ink);
        }
        // the strip, and the line under it
        ink.setStyle(Paint.Style.FILL);
        ink.setColor(halo ? Color.argb(235, 6, 11, 20) : Color.argb(235, 10, 13, 10));
        canvas.drawRect(0, 0, width, strip, ink);
        ink.setStyle(Paint.Style.STROKE);
        ink.setStrokeWidth(1.5f);
        ink.setColor(alpha(accent, 0.59f));
        canvas.drawLine(0, strip, width, strip, ink);
        // just outside the picture: brackets at its corners (halo) or a rounded frame (xbox)
        float gap = 4;
        RectF picture = pictureRect;
        if (halo) {
            Path shield = new Path();
            shield.moveTo(x - 150, strip);
            shield.lineTo(x - 130, strip - 8);
            shield.lineTo(x + 130, strip - 8);
            shield.lineTo(x + 150, strip);
            ink.setColor(alpha(accent, 0.43f));
            canvas.drawPath(shield, ink);
            ink.setStrokeWidth(2);
            ink.setColor(alpha(accent, 0.78f));
            float reach = 22;
            for (int corner = 0; corner < 4; corner++) {
                float side = corner % 2 == 0 ? 1 : -1;
                float cornerX = corner % 2 == 0 ? picture.left + 6 : picture.right - 6;
                float lineY = corner < 2 ? picture.top - gap : picture.bottom + gap;
                float out = corner < 2 ? -1 : 1;
                Path bracket = new Path();
                bracket.moveTo(cornerX, lineY + out * reach / 2);
                bracket.lineTo(cornerX, lineY);
                bracket.lineTo(cornerX + side * reach, lineY);
                canvas.drawPath(bracket, ink);
            }
            ink.setStrokeWidth(1.5f);
            ink.setColor(alpha(accent, 0.51f));
            canvas.drawLine(picture.left, picture.bottom + 0.75f, picture.right, picture.bottom + 0.75f, ink);
        } else {
            ink.setStrokeWidth(2);
            ink.setColor(alpha(accent, 0.67f));
            canvas.drawRoundRect(picture.left - gap, picture.top - gap, picture.right + gap, picture.bottom + gap, 10, 10,
                ink);
        }
    }

    @Override
    protected void onDraw(Canvas canvas) {
        if (!showing && !editing) {
            if (!pictureRect.isEmpty()) {
                canvas.save();
                canvas.scale(scale, scale);
                drawFrame(canvas);
                canvas.restore();
            }
            return;
        }
        float opacity = layout.opacity * (editing ? 1f : idle);
        canvas.save();
        canvas.scale(scale, scale);
        drawFrame(canvas);
        if (!hidden || editing) {
            if (layout.shown(TouchLayout.LEFT))
                drawStick(canvas, opacity);
            for (int i = 0; i < layout.size(); i++) {
                if (!layout.shown(i) || layout.type(i) == TouchLayout.LEFT || (!editing && hiddenNow(layout.type(i))))
                    continue;
                control(canvas, i, pressed(i) || dragControl == i, opacity);
            }
        }
        drawToolbar(canvas);
        canvas.restore();
    }
}
