const tabs = [...document.querySelectorAll(".tab-btn")];
const panels = {
  status: document.getElementById("tab-status"),
  config: document.getElementById("tab-config"),
  files: document.getElementById("tab-files"),
};

const statusNote = document.getElementById("statusNote");
const crumbs = document.getElementById("crumbs");
const fileRows = document.getElementById("fileRows");
const fileSummary = document.getElementById("fileSummary");
const gammaCtrl = document.getElementById("gammaCtrl");
const gammaVal = document.getElementById("gammaVal");
const uploadModeSel = document.getElementById("uploadMode");
const ditherModeSel = document.getElementById("ditherMode");
const uploadExtra = document.getElementById("uploadExtra");
const uploadBtn = document.querySelector("button[onclick='uploadFile()']");
const uploadBox = document.getElementById("uploadBox");
const ditherHint = document.getElementById("ditherHint");
const gammaHint = document.getElementById("gammaHint");
const modeNote = document.getElementById("modeNote");

const ssid = document.getElementById("ssid");
const pass = document.getElementById("pass");
const sec = document.getElementById("sec");
const calendarSec = document.getElementById("calendarSec");
const calendarUrl = document.getElementById("calendarUrl");
const calendarLayout = document.getElementById("calendarLayout");
const weatherLocation = document.getElementById("weatherLocation");
const weatherLat = document.getElementById("weatherLat");
const weatherLon = document.getElementById("weatherLon");
const wurl = document.getElementById("wurl");
const cfgBox = document.getElementById("cfgBox");
const scheduleTitle = document.getElementById("scheduleTitle");
const scheduleTime = document.getElementById("scheduleTime");
const scheduleColor = document.getElementById("scheduleColor");
const scheduleRepeat = document.getElementById("scheduleRepeat");
const scheduleDate = document.getElementById("scheduleDate");
const scheduleWeekday = document.getElementById("scheduleWeekday");
const scheduleRows = document.getElementById("scheduleRows");
const scheduleSummary = document.getElementById("scheduleSummary");
const uiLanguage = document.getElementById("uiLanguage");

let currentDir = "/pic";
let latestStatusState = "";
let currentLanguage = "zh";
let lastScheduleItems = [];
let lastFileItems = [];
let activeTabKey = "status";
let cfgLoadedOnce = false;
let filesLoadedOnce = false;
let schedulesLoadedOnce = false;
let statusPollTimer = null;
let statusPollInFlight = false;

const STATUS_POLL_MS_ACTIVE = 8000;
const STATUS_POLL_MS_IDLE = 30000;
const STATUS_POLL_MS_HIDDEN = 60000;

const I18N = {
  zh: {
    "app.title": "E-paper 管理台",
    "app.subtitle": "默认首页：设备状态",
    "tabs.status": "设备状态",
    "tabs.config": "配置",
    "tabs.files": "目录",
    "status.refresh": "刷新状态",
    "status.stop_portal": "停止 WiFi 门户",
    "status.current_state": "当前设备状态",
    "status.mode": "运行模式",
    "status.ip": "设备 IP",
    "status.ap_ip": "AP IP",
    "status.sd": "SD 卡状态",
    "status.sd_space": "SD 空间",
    "status.uptime": "已运行时长",
    "status.timeout": "超时剩余",
    "status.temp": "温度",
    "status.humidity": "湿度",
    "status.battery": "电量",
    "state.idle": "空闲",
    "state.ap_running": "AP 模式",
    "state.sta_connecting": "STA 连接中",
    "state.sta_running": "STA 在线",
    "cfg.network.title": "网络配置",
    "cfg.network.ssid": "STA SSID",
    "cfg.network.ssid_ph": "连接到现有 WiFi",
    "cfg.network.pass": "STA 密码",
    "cfg.network.pass_ph": "输入 WiFi 密码",
    "cfg.network.language": "页面语言",
    "cfg.network.lang_zh": "中文",
    "cfg.network.lang_en": "English",
    "cfg.network.lang_fr": "Francais",
    "cfg.photo.title": "图片轮播",
    "cfg.photo.interval": "轮播间隔（秒）",
    "cfg.calendar.title": "日历事件",
    "cfg.calendar.layout": "日历布局",
    "cfg.calendar.layout_landscape": "横屏半屏（左日历 / 右日程）",
    "cfg.calendar.layout_portrait": "竖屏半屏（上日历 / 下日程）",
    "cfg.calendar.refresh_sec": "日历刷新间隔（秒）",
    "cfg.calendar.url": "日历数据 URL（预留）",
    "cfg.calendar.url_ph": "保留字段，当前离线日历不依赖该 URL",
    "cfg.calendar.note": "离线日历仅依赖设备本地时间；联网后会自动校时。",
    "cfg.schedule.title": "手动日程",
    "cfg.schedule.name": "日程标题",
    "cfg.schedule.name_ph": "例如：团队周会",
    "cfg.schedule.time": "时间",
    "cfg.schedule.color": "颜色",
    "cfg.schedule.color_red": "红色（重要提醒）",
    "cfg.schedule.color_blue": "蓝色（工作安排）",
    "cfg.schedule.color_green": "绿色（日常任务）",
    "cfg.schedule.color_yellow": "黄色（弹性事项）",
    "cfg.schedule.color_black": "黑色（默认）",
    "cfg.schedule.color_white": "白色（弱提示）",
    "cfg.schedule.repeat": "重复机制",
    "cfg.schedule.repeat_weekly": "每周重复",
    "cfg.schedule.repeat_daily": "每天重复",
    "cfg.schedule.repeat_once": "仅一次",
    "cfg.schedule.date": "日期（仅一次）",
    "cfg.schedule.weekday": "星期（每周重复）",
    "weekday.mon": "周一",
    "weekday.tue": "周二",
    "weekday.wed": "周三",
    "weekday.thu": "周四",
    "weekday.fri": "周五",
    "weekday.sat": "周六",
    "weekday.sun": "周日",
    "cfg.schedule.add": "添加日程",
    "cfg.schedule.reload": "刷新日程",
    "cfg.schedule.col_time": "时间",
    "cfg.schedule.col_name": "标题",
    "cfg.schedule.col_rule": "规则",
    "cfg.schedule.col_color": "颜色",
    "cfg.schedule.col_action": "操作",
    "cfg.schedule.empty": "暂无日程",
    "cfg.weather.title": "天气与定位",
    "cfg.weather.city": "城市",
    "cfg.weather.city_ph": "如 Shanghai 或 上海",
    "cfg.weather.lat": "纬度",
    "cfg.weather.lon": "经度",
    "cfg.weather.url": "天气接口 URL",
    "cfg.weather.url_ph": "可手动填写，或由城市/经纬度自动生成",
    "cfg.weather.resolve": "解析城市",
    "cfg.weather.test": "测试天气服务",
    "cfg.weather.note": "时区将根据天气接口返回值自动识别，并用于 WiFi 联网校时。",
    "cfg.waiting": "等待操作...",
    "cfg.save": "保存设置",
    "cfg.reboot": "重启设备",
    "files.up": "上一级",
    "files.new_folder": "新建文件夹",
    "files.reload": "刷新目录",
    "files.col_name": "名称",
    "files.col_size": "大小",
    "files.col_type": "类型",
    "files.col_action": "操作",
    "files.loading": "等待加载...",
    "upload.mode_normal": "普通文件上传",
    "upload.mode_fit": "图片缩放上传",
    "upload.mode_crop": "图片裁剪上传",
    "upload.exec": "执行上传",
    "upload.algo": "抖动算法",
    "upload.algo_fs": "Floyd-Steinberg（通用默认 / 细节锐利）",
    "upload.algo_atkinson": "Atkinson（插画 / 图标 / 风格化）",
    "upload.algo_jjn": "Jarvis-Judice-Ninke（照片 / 渐变平滑）",
    "upload.gamma": "Gamma",
    "upload.gamma_tip": "推荐值：1.00 通用；0.90~0.98 提亮暗部；1.05~1.15 增强层次；过高会压暗暗部细节。",
    "upload.waiting": "等待上传...",
    "status.mounted": "已挂载",
    "status.unmounted": "未挂载",
    "status.not_connected": "未接入",
    "status.sd_space_fmt": "{used} / {total}（剩余 {free}）",
    "status.battery_full_fmt": "{pct}%（{mv} mV，IO{pin}）",
    "status.battery_mv_fmt": "{mv} mV（IO{pin}，仅显示电压）",
    "common.loading": "正在加载...",
    "common.request_failed": "请求失败",
    "common.delete": "删除",
    "common.open": "打开",
    "common.download": "下载",
    "common.folder": "目录",
    "common.file": "文件",
    "common.current_dir_fmt": "当前目录 {dir}，共 {count} 项",
    "common.dir_empty": "目录为空",
    "common.dir_read_failed": "目录读取失败",
    "common.schedule_loaded_zero": "已加载 0 条日程",
    "common.schedule_loaded_fmt": "已加载 {count} 条日程",
    "common.schedule_read_failed": "日程读取失败",
    "common.add_failed": "添加失败",
    "common.delete_failed": "删除失败",
    "common.please_enter_city": "请先输入城市名称。",
    "common.resolving_city": "正在解析城市...",
    "common.city_resolve_failed": "城市解析失败。请确认设备处于 STA 联网，或手动填写经纬度。",
    "common.city_resolve_ok": "城市解析成功，已回填经纬度和天气 URL。",
    "common.save_failed": "保存失败，请检查 STA 联网状态、城市名或经纬度。",
    "common.weather_sta_only": "天气测试仅在 STA 联网后可用。",
    "common.not_sta_notice": "当前不是 STA 联网状态。",
    "common.weather_test_failed": "天气服务测试失败",
    "common.weather_tz_updated": "（已自动更新）",
    "common.weather_sync_ok_fmt": "成功，设备时间 {time}",
    "common.weather_sync_fail_fmt": "失败（{err}）",
    "common.weather_report": "天气服务测试成功\nHTTP 状态: {http}\n设备 IP: {ip}\n请求地址: {url}\n识别时区: {tz}\n联网校时: {sync}\n响应预览: {preview}",
    "common.stop_sent": "停止请求已发送。",
    "common.portal_closing": "门户即将关闭。",
    "common.request_done": "请求已完成。",
    "common.saved": "设置已保存。",
    "common.rebooting": "重启请求已发送。",
    "common.reboot_confirm": "确认立即重启设备吗？",
    "common.new_folder_prompt": "新建文件夹名称",
    "common.del_dir_confirm": "确认删除目录及其内容？\n{path}",
    "common.del_file_confirm": "确认删除文件？\n{path}",
    "common.add_title_required": "请先输入日程标题",
    "common.bad_time": "时间格式错误，请使用 HH:MM",
    "common.bad_once_date": "仅一次日程需要有效日期",
    "common.bad_weekday": "每周重复需要有效星期",
    "common.add_failed_check": "添加失败，请检查连接状态",
    "common.del_schedule_confirm": "确认删除日程 #{id}？",
    "common.del_failed_check": "删除失败，请检查连接状态",
    "repeat.once": "仅一次",
    "repeat.daily": "每天",
    "repeat.weekly": "每周",
    "color.red": "红",
    "color.blue": "蓝",
    "color.green": "绿",
    "color.yellow": "黄",
    "color.black": "黑",
    "color.white": "白",
    "upload.hint_fs": "通用默认，细节锐利。",
    "upload.hint_atkinson": "适合插画、图标、风格化。",
    "upload.hint_jjn": "适合照片和渐变平滑。",
    "upload.gamma_hint_low": "偏亮：暗部会被抬起。",
    "upload.gamma_hint_midhigh": "偏重：层次更实，适合风景。",
    "upload.gamma_hint_high": "较重：对比更强，暗部细节更易丢失。",
    "upload.mode_note_normal": "普通文件上传会直接保存到当前目录。",
    "upload.mode_note_fit": "图片按比例缩放到 800x480 后转 .epd4 保存到 /pic。",
    "upload.mode_note_crop": "图片优先铺满 800x480（可能裁边）后转 .epd4 保存到 /pic。",
    "upload.pick_file": "请选择要上传的文件",
    "upload.only_images": "缩放/裁剪上传仅支持 bmp/jpg/png",
    "upload.preprocessing": "正在预处理并转换为 EPD4...",
    "upload.preprocess_failed": "预处理失败: {err}",
  },
  en: {
    "app.title": "E-paper Console",
    "app.subtitle": "Default tab: Status",
    "tabs.status": "Status",
    "tabs.config": "Config",
    "tabs.files": "Files",
    "status.refresh": "Refresh",
    "status.stop_portal": "Stop WiFi Portal",
    "status.current_state": "Current device status",
    "status.mode": "Mode",
    "status.ip": "Device IP",
    "status.ap_ip": "AP IP",
    "status.sd": "SD State",
    "status.sd_space": "SD Usage",
    "status.uptime": "Uptime",
    "status.timeout": "Timeout Left",
    "status.temp": "Temperature",
    "status.humidity": "Humidity",
    "status.battery": "Battery",
    "state.idle": "Idle",
    "state.ap_running": "AP Mode",
    "state.sta_connecting": "STA Connecting",
    "state.sta_running": "STA Online",
    "cfg.network.title": "Network",
    "cfg.network.ssid": "STA SSID",
    "cfg.network.ssid_ph": "Connect to existing WiFi",
    "cfg.network.pass": "STA Password",
    "cfg.network.pass_ph": "Enter WiFi password",
    "cfg.network.language": "UI Language",
    "cfg.network.lang_zh": "Chinese",
    "cfg.network.lang_en": "English",
    "cfg.network.lang_fr": "French",
    "cfg.photo.title": "Photo Slideshow",
    "cfg.photo.interval": "Interval (seconds)",
    "cfg.calendar.title": "Calendar",
    "cfg.calendar.layout": "Layout",
    "cfg.calendar.layout_landscape": "Landscape Split (calendar left / schedule right)",
    "cfg.calendar.layout_portrait": "Portrait Split (calendar top / schedule bottom)",
    "cfg.calendar.refresh_sec": "Refresh interval (seconds)",
    "cfg.calendar.url": "Calendar URL (reserved)",
    "cfg.calendar.url_ph": "Reserved field; offline calendar does not use this URL now",
    "cfg.calendar.note": "Offline calendar uses local time only; network sync updates clock automatically.",
    "cfg.schedule.title": "Manual Schedule",
    "cfg.schedule.name": "Title",
    "cfg.schedule.name_ph": "Example: Team Weekly Meeting",
    "cfg.schedule.time": "Time",
    "cfg.schedule.color": "Color",
    "cfg.schedule.color_red": "Red (important)",
    "cfg.schedule.color_blue": "Blue (work)",
    "cfg.schedule.color_green": "Green (routine)",
    "cfg.schedule.color_yellow": "Yellow (flexible)",
    "cfg.schedule.color_black": "Black (default)",
    "cfg.schedule.color_white": "White (soft)",
    "cfg.schedule.repeat": "Repeat",
    "cfg.schedule.repeat_weekly": "Weekly",
    "cfg.schedule.repeat_daily": "Daily",
    "cfg.schedule.repeat_once": "Once",
    "cfg.schedule.date": "Date (once)",
    "cfg.schedule.weekday": "Weekday (weekly)",
    "weekday.mon": "Mon",
    "weekday.tue": "Tue",
    "weekday.wed": "Wed",
    "weekday.thu": "Thu",
    "weekday.fri": "Fri",
    "weekday.sat": "Sat",
    "weekday.sun": "Sun",
    "cfg.schedule.add": "Add",
    "cfg.schedule.reload": "Reload",
    "cfg.schedule.col_time": "Time",
    "cfg.schedule.col_name": "Title",
    "cfg.schedule.col_rule": "Rule",
    "cfg.schedule.col_color": "Color",
    "cfg.schedule.col_action": "Action",
    "cfg.schedule.empty": "No schedule",
    "cfg.weather.title": "Weather & Location",
    "cfg.weather.city": "City",
    "cfg.weather.city_ph": "e.g. Shanghai",
    "cfg.weather.lat": "Latitude",
    "cfg.weather.lon": "Longitude",
    "cfg.weather.url": "Weather URL",
    "cfg.weather.url_ph": "Manual URL or auto-generated from city/coordinates",
    "cfg.weather.resolve": "Resolve City",
    "cfg.weather.test": "Test Weather",
    "cfg.weather.note": "Timezone is auto-detected from weather API and used for clock sync.",
    "cfg.waiting": "Waiting...",
    "cfg.save": "Save",
    "cfg.reboot": "Reboot",
    "files.up": "Up",
    "files.new_folder": "New Folder",
    "files.reload": "Refresh",
    "files.col_name": "Name",
    "files.col_size": "Size",
    "files.col_type": "Type",
    "files.col_action": "Action",
    "files.loading": "Loading...",
    "upload.mode_normal": "Upload File",
    "upload.mode_fit": "Image Fit Upload",
    "upload.mode_crop": "Image Crop Upload",
    "upload.exec": "Upload",
    "upload.algo": "Dithering",
    "upload.algo_fs": "Floyd-Steinberg (general / sharp detail)",
    "upload.algo_atkinson": "Atkinson (icons / illustration / stylized)",
    "upload.algo_jjn": "Jarvis-Judice-Ninke (photo / smooth gradient)",
    "upload.gamma": "Gamma",
    "upload.gamma_tip": "Recommended: 1.00 baseline; 0.90~0.98 brighter shadows; 1.05~1.15 stronger layering; too high loses dark detail.",
    "upload.waiting": "Waiting for upload...",
    "status.mounted": "Mounted",
    "status.unmounted": "Not mounted",
    "status.not_connected": "Not connected",
    "status.sd_space_fmt": "{used} / {total} (free {free})",
    "status.battery_full_fmt": "{pct}% ({mv} mV, IO{pin})",
    "status.battery_mv_fmt": "{mv} mV (IO{pin}, voltage only)",
    "common.loading": "Loading...",
    "common.request_failed": "Request failed",
    "common.delete": "Delete",
    "common.open": "Open",
    "common.download": "Download",
    "common.folder": "Folder",
    "common.file": "File",
    "common.current_dir_fmt": "Current: {dir}, {count} item(s)",
    "common.dir_empty": "Folder is empty",
    "common.dir_read_failed": "Failed to read directory",
    "common.schedule_loaded_zero": "Loaded 0 schedule item(s)",
    "common.schedule_loaded_fmt": "Loaded {count} schedule item(s)",
    "common.schedule_read_failed": "Failed to load schedule",
    "common.add_failed": "Add failed",
    "common.delete_failed": "Delete failed",
    "common.please_enter_city": "Please enter a city name first.",
    "common.resolving_city": "Resolving city...",
    "common.city_resolve_failed": "City resolve failed. Ensure STA is online or fill coordinates manually.",
    "common.city_resolve_ok": "City resolved. Coordinates and weather URL updated.",
    "common.save_failed": "Save failed. Check STA connectivity, city name or coordinates.",
    "common.weather_sta_only": "Weather test is only available in STA online mode.",
    "common.not_sta_notice": "Current mode is not STA online.",
    "common.weather_test_failed": "Weather test failed",
    "common.weather_tz_updated": " (auto-updated)",
    "common.weather_sync_ok_fmt": "OK, device time {time}",
    "common.weather_sync_fail_fmt": "Failed ({err})",
    "common.weather_report": "Weather test successful\nHTTP: {http}\nDevice IP: {ip}\nURL: {url}\nTimezone: {tz}\nClock sync: {sync}\nPreview: {preview}",
    "common.stop_sent": "Stop request sent.",
    "common.portal_closing": "Portal is closing soon.",
    "common.request_done": "Request completed.",
    "common.saved": "Settings saved.",
    "common.rebooting": "Reboot request sent.",
    "common.reboot_confirm": "Reboot device now?",
    "common.new_folder_prompt": "New folder name",
    "common.del_dir_confirm": "Delete folder and all contents?\n{path}",
    "common.del_file_confirm": "Delete file?\n{path}",
    "common.add_title_required": "Please input schedule title",
    "common.bad_time": "Invalid time. Use HH:MM",
    "common.bad_once_date": "One-time schedule requires a valid date",
    "common.bad_weekday": "Weekly schedule requires a valid weekday",
    "common.add_failed_check": "Add failed. Check connection.",
    "common.del_schedule_confirm": "Delete schedule #{id}?",
    "common.del_failed_check": "Delete failed. Check connection.",
    "repeat.once": "Once",
    "repeat.daily": "Daily",
    "repeat.weekly": "Weekly",
    "color.red": "Red",
    "color.blue": "Blue",
    "color.green": "Green",
    "color.yellow": "Yellow",
    "color.black": "Black",
    "color.white": "White",
    "upload.hint_fs": "General default, sharp detail.",
    "upload.hint_atkinson": "Good for icons, illustration, stylized images.",
    "upload.hint_jjn": "Good for photos and smoother gradients.",
    "upload.gamma_hint_low": "Brighter: shadows are lifted.",
    "upload.gamma_hint_midhigh": "Heavier: stronger layers, good for landscapes.",
    "upload.gamma_hint_high": "Too heavy: stronger contrast, dark details may be lost.",
    "upload.mode_note_normal": "Normal upload saves file to current directory.",
    "upload.mode_note_fit": "Fit mode scales image to 800x480 and saves as .epd4 in /pic.",
    "upload.mode_note_crop": "Crop mode fills 800x480 (may cut edges) and saves .epd4 in /pic.",
    "upload.pick_file": "Please select a file",
    "upload.only_images": "Fit/Crop mode only supports bmp/jpg/png",
    "upload.preprocessing": "Preprocessing and converting to EPD4...",
    "upload.preprocess_failed": "Preprocess failed: {err}",
  },
  fr: {
    "app.title": "Console E-paper",
    "app.subtitle": "Onglet par defaut : Etat",
    "tabs.status": "Etat",
    "tabs.config": "Config",
    "tabs.files": "Fichiers",
    "status.refresh": "Actualiser",
    "status.stop_portal": "Arreter le portail WiFi",
    "status.current_state": "Etat actuel de l appareil",
    "status.mode": "Mode",
    "status.ip": "IP appareil",
    "status.ap_ip": "IP AP",
    "status.sd": "Etat SD",
    "status.sd_space": "Espace SD",
    "status.uptime": "Temps actif",
    "status.timeout": "Temps restant",
    "status.temp": "Temperature",
    "status.humidity": "Humidite",
    "status.battery": "Batterie",
    "state.idle": "Inactif",
    "state.ap_running": "Mode AP",
    "state.sta_connecting": "Connexion STA",
    "state.sta_running": "STA en ligne",
    "cfg.network.title": "Reseau",
    "cfg.network.ssid": "SSID STA",
    "cfg.network.ssid_ph": "Se connecter au WiFi existant",
    "cfg.network.pass": "Mot de passe STA",
    "cfg.network.pass_ph": "Entrer le mot de passe WiFi",
    "cfg.network.language": "Langue UI",
    "cfg.network.lang_zh": "Chinois",
    "cfg.network.lang_en": "Anglais",
    "cfg.network.lang_fr": "Francais",
    "cfg.photo.title": "Diaporama",
    "cfg.photo.interval": "Intervalle (secondes)",
    "cfg.calendar.title": "Calendrier",
    "cfg.calendar.layout": "Disposition",
    "cfg.calendar.layout_landscape": "Partage paysage (calendrier gauche / planning droite)",
    "cfg.calendar.layout_portrait": "Partage portrait (calendrier haut / planning bas)",
    "cfg.calendar.refresh_sec": "Intervalle de rafraichissement (secondes)",
    "cfg.calendar.url": "URL calendrier (reserve)",
    "cfg.calendar.url_ph": "Champ reserve ; le mode hors ligne ne l utilise pas",
    "cfg.calendar.note": "Le calendrier hors ligne utilise l heure locale ; la connexion reseau synchronise l horloge.",
    "cfg.schedule.title": "Planning manuel",
    "cfg.schedule.name": "Titre",
    "cfg.schedule.name_ph": "Exemple : reunion hebdo equipe",
    "cfg.schedule.time": "Heure",
    "cfg.schedule.color": "Couleur",
    "cfg.schedule.color_red": "Rouge (important)",
    "cfg.schedule.color_blue": "Bleu (travail)",
    "cfg.schedule.color_green": "Vert (routine)",
    "cfg.schedule.color_yellow": "Jaune (souple)",
    "cfg.schedule.color_black": "Noir (defaut)",
    "cfg.schedule.color_white": "Blanc (leger)",
    "cfg.schedule.repeat": "Repetition",
    "cfg.schedule.repeat_weekly": "Hebdomadaire",
    "cfg.schedule.repeat_daily": "Quotidienne",
    "cfg.schedule.repeat_once": "Unique",
    "cfg.schedule.date": "Date (unique)",
    "cfg.schedule.weekday": "Jour (hebdo)",
    "weekday.mon": "Lun",
    "weekday.tue": "Mar",
    "weekday.wed": "Mer",
    "weekday.thu": "Jeu",
    "weekday.fri": "Ven",
    "weekday.sat": "Sam",
    "weekday.sun": "Dim",
    "cfg.schedule.add": "Ajouter",
    "cfg.schedule.reload": "Recharger",
    "cfg.schedule.col_time": "Heure",
    "cfg.schedule.col_name": "Titre",
    "cfg.schedule.col_rule": "Regle",
    "cfg.schedule.col_color": "Couleur",
    "cfg.schedule.col_action": "Action",
    "cfg.schedule.empty": "Aucun planning",
    "cfg.weather.title": "Meteo et position",
    "cfg.weather.city": "Ville",
    "cfg.weather.city_ph": "ex. Shanghai",
    "cfg.weather.lat": "Latitude",
    "cfg.weather.lon": "Longitude",
    "cfg.weather.url": "URL meteo",
    "cfg.weather.url_ph": "URL manuelle ou generee depuis ville/coordonnees",
    "cfg.weather.resolve": "Resoudre ville",
    "cfg.weather.test": "Tester meteo",
    "cfg.weather.note": "Le fuseau horaire est detecte automatiquement par l API meteo et utilise pour la sync.",
    "cfg.waiting": "En attente...",
    "cfg.save": "Enregistrer",
    "cfg.reboot": "Redemarrer",
    "files.up": "Niveau superieur",
    "files.new_folder": "Nouveau dossier",
    "files.reload": "Rafraichir",
    "files.col_name": "Nom",
    "files.col_size": "Taille",
    "files.col_type": "Type",
    "files.col_action": "Action",
    "files.loading": "Chargement...",
    "upload.mode_normal": "Televerser fichier",
    "upload.mode_fit": "Televerser image adaptee",
    "upload.mode_crop": "Televerser image recadree",
    "upload.exec": "Televerser",
    "upload.algo": "Algorithme",
    "upload.algo_fs": "Floyd-Steinberg (general / detail net)",
    "upload.algo_atkinson": "Atkinson (icones / illustration / style)",
    "upload.algo_jjn": "Jarvis-Judice-Ninke (photo / degrade doux)",
    "upload.gamma": "Gamma",
    "upload.gamma_tip": "Valeurs conseillees : 1.00 general ; 0.90~0.98 eclaircit les ombres ; 1.05~1.15 renforce les couches ; trop haut perd les details sombres.",
    "upload.waiting": "En attente de televersement...",
    "status.mounted": "Monte",
    "status.unmounted": "Non monte",
    "status.not_connected": "Non connecte",
    "status.sd_space_fmt": "{used} / {total} (libre {free})",
    "status.battery_full_fmt": "{pct}% ({mv} mV, IO{pin})",
    "status.battery_mv_fmt": "{mv} mV (IO{pin}, tension seule)",
    "common.loading": "Chargement...",
    "common.request_failed": "Echec de requete",
    "common.delete": "Supprimer",
    "common.open": "Ouvrir",
    "common.download": "Telecharger",
    "common.folder": "Dossier",
    "common.file": "Fichier",
    "common.current_dir_fmt": "Dossier courant {dir}, {count} element(s)",
    "common.dir_empty": "Dossier vide",
    "common.dir_read_failed": "Lecture du dossier echouee",
    "common.schedule_loaded_zero": "0 planning charge",
    "common.schedule_loaded_fmt": "{count} planning(s) charge(s)",
    "common.schedule_read_failed": "Chargement du planning echoue",
    "common.add_failed": "Ajout echoue",
    "common.delete_failed": "Suppression echouee",
    "common.please_enter_city": "Veuillez saisir un nom de ville.",
    "common.resolving_city": "Resolution de la ville...",
    "common.city_resolve_failed": "Echec de resolution. Verifiez le mode STA en ligne ou saisissez les coordonnees manuellement.",
    "common.city_resolve_ok": "Ville resolue. Coordonnees et URL meteo mises a jour.",
    "common.save_failed": "Enregistrement echoue. Verifiez la connexion STA, la ville ou les coordonnees.",
    "common.weather_sta_only": "Le test meteo est disponible uniquement en mode STA en ligne.",
    "common.not_sta_notice": "Le mode actuel n est pas STA en ligne.",
    "common.weather_test_failed": "Test meteo echoue",
    "common.weather_tz_updated": " (mise a jour auto)",
    "common.weather_sync_ok_fmt": "OK, heure appareil {time}",
    "common.weather_sync_fail_fmt": "Echec ({err})",
    "common.weather_report": "Test meteo reussi\nHTTP: {http}\nIP appareil: {ip}\nURL: {url}\nFuseau: {tz}\nSync horloge: {sync}\nApercu: {preview}",
    "common.stop_sent": "Requete d arret envoyee.",
    "common.portal_closing": "Le portail va se fermer.",
    "common.request_done": "Requete terminee.",
    "common.saved": "Parametres enregistres.",
    "common.rebooting": "Requete de redemarrage envoyee.",
    "common.reboot_confirm": "Redemarrer l appareil maintenant ?",
    "common.new_folder_prompt": "Nom du nouveau dossier",
    "common.del_dir_confirm": "Supprimer le dossier et son contenu ?\n{path}",
    "common.del_file_confirm": "Supprimer le fichier ?\n{path}",
    "common.add_title_required": "Veuillez saisir un titre",
    "common.bad_time": "Heure invalide. Utilisez HH:MM",
    "common.bad_once_date": "Un evenement unique exige une date valide",
    "common.bad_weekday": "Un evenement hebdo exige un jour valide",
    "common.add_failed_check": "Ajout echoue. Verifiez la connexion.",
    "common.del_schedule_confirm": "Supprimer le planning #{id} ?",
    "common.del_failed_check": "Suppression echouee. Verifiez la connexion.",
    "repeat.once": "Unique",
    "repeat.daily": "Quotidien",
    "repeat.weekly": "Hebdo",
    "color.red": "Rouge",
    "color.blue": "Bleu",
    "color.green": "Vert",
    "color.yellow": "Jaune",
    "color.black": "Noir",
    "color.white": "Blanc",
    "upload.hint_fs": "Defaut general, details nets.",
    "upload.hint_atkinson": "Ideal pour icones, illustrations et style graphique.",
    "upload.hint_jjn": "Ideal pour photos et degrades plus doux.",
    "upload.gamma_hint_low": "Plus clair : les ombres montent.",
    "upload.gamma_hint_midhigh": "Plus dense : couches renforcees, bon pour paysages.",
    "upload.gamma_hint_high": "Tres dense : contraste fort, details sombres perdus.",
    "upload.mode_note_normal": "Le mode normal enregistre le fichier dans le dossier courant.",
    "upload.mode_note_fit": "Le mode adapte redimensionne a 800x480 et enregistre en .epd4 dans /pic.",
    "upload.mode_note_crop": "Le mode recadre remplit 800x480 (bords coupes possibles) puis enregistre en .epd4 dans /pic.",
    "upload.pick_file": "Veuillez selectionner un fichier",
    "upload.only_images": "Le mode adapte/recadre accepte seulement bmp/jpg/png",
    "upload.preprocessing": "Pretraitement et conversion vers EPD4...",
    "upload.preprocess_failed": "Echec du pretraitement : {err}",
  },
};

function normalizeLang(raw) {
  const lang = String(raw || "").trim().toLowerCase();
  return (lang === "en" || lang === "fr") ? lang : "zh";
}

function t(key) {
  const bucket = I18N[currentLanguage] || I18N.zh;
  if (Object.prototype.hasOwnProperty.call(bucket, key)) {
    return bucket[key];
  }
  if (Object.prototype.hasOwnProperty.call(I18N.zh, key)) {
    return I18N.zh[key];
  }
  return key;
}

function fmt(key, vars = {}) {
  let text = t(key);
  Object.keys(vars).forEach((name) => {
    text = text.replaceAll(`{${name}}`, String(vars[name]));
  });
  return text;
}

function languageTag(lang) {
  if (lang === "en") return "en";
  if (lang === "fr") return "fr";
  return "zh-CN";
}

function applyI18n(lang) {
  currentLanguage = normalizeLang(lang);
  document.documentElement.lang = languageTag(currentLanguage);

  document.querySelectorAll("[data-i18n]").forEach((el) => {
    const key = el.getAttribute("data-i18n");
    if (!key) return;
    el.textContent = t(key);
  });
  document.querySelectorAll("[data-i18n-placeholder]").forEach((el) => {
    const key = el.getAttribute("data-i18n-placeholder");
    if (!key) return;
    el.setAttribute("placeholder", t(key));
  });

  syncGammaHint();
  syncUploadOptions();
  renderScheduleRows(lastScheduleItems);
  renderRows(lastFileItems);
  if (lastFileItems.length > 0) {
    fileSummary.textContent = fmt("common.current_dir_fmt", { dir: currentDir, count: lastFileItems.length });
  }
}

function weekdayLabel(index) {
  const keys = ["weekday.mon", "weekday.tue", "weekday.wed", "weekday.thu", "weekday.fri", "weekday.sat", "weekday.sun"];
  if (index < 0 || index >= keys.length) return "--";
  return t(keys[index]);
}

function colorLabel(color) {
  const key = `color.${String(color || "").toLowerCase()}`;
  const value = t(key);
  return value === key ? String(color || "") : value;
}

function stateLabel(raw) {
  const key = `state.${String(raw || "").toLowerCase()}`;
  const value = t(key);
  if (value === key) return raw || "--";
  return value;
}

function setNotice(text) {
  if (!text) {
    statusNote.style.display = "none";
    statusNote.textContent = "";
    return;
  }
  statusNote.style.display = "block";
  statusNote.textContent = text;
}

function normalizeDir(path) {
  let out = path || "/";
  if (!out.startsWith("/")) out = "/" + out;
  out = out.replace(/\/+/, "/");
  out = out.replace(/\/+/g, "/");
  if (out.length > 1 && out.endsWith("/")) out = out.slice(0, -1);
  return out || "/";
}

function parentDir(path) {
  const dir = normalizeDir(path);
  if (dir === "/") return "/";
  const idx = dir.lastIndexOf("/");
  return idx <= 0 ? "/" : dir.slice(0, idx);
}

function joinPath(dir, name) {
  const base = normalizeDir(dir);
  if (!name) return base;
  return normalizeDir((base === "/" ? "" : base) + "/" + name);
}

function formatSize(bytes) {
  const n = Number(bytes || 0);
  if (n < 1024) return `${n} B`;
  if (n < 1024 * 1024) return `${(n / 1024).toFixed(1)} KB`;
  return `${(n / (1024 * 1024)).toFixed(1)} MB`;
}

function fmtMs(ms) {
  const s = Math.floor((ms || 0) / 1000);
  return `${Math.floor(s / 3600)}h ${Math.floor((s % 3600) / 60)}m ${s % 60}s`;
}

function buildOpenMeteoUrl(lat, lon) {
  const latText = Number(lat).toFixed(4);
  const lonText = Number(lon).toFixed(4);
  return `https://api.open-meteo.com/v1/forecast?latitude=${latText}&longitude=${lonText}&current=temperature_2m,relative_humidity_2m,weather_code&timezone=auto`;
}

async function resolveCity() {
  const city = (weatherLocation.value || "").trim();
  if (!city) {
    cfgBox.textContent = t("common.please_enter_city");
    return false;
  }
  cfgBox.textContent = t("common.resolving_city");
  const r = await fetch("/api/geocode?city=" + encodeURIComponent(city));
  const txt = await r.text();
  let j = null;
  try { j = JSON.parse(txt); } catch {}
  if (!j || !j.ok) {
    cfgBox.textContent = t("common.city_resolve_failed");
    return false;
  }
  weatherLocation.value = j.city || city;
  weatherLat.value = j.lat || "";
  weatherLon.value = j.lon || "";
  wurl.value = j.weather_url || buildOpenMeteoUrl(j.lat, j.lon);
  cfgBox.textContent = t("common.city_resolve_ok");
  return true;
}

function ditherAdviceText(mode) {
  if (mode === "atkinson") return t("upload.hint_atkinson");
  if (mode === "jjn") return t("upload.hint_jjn");
  return t("upload.hint_fs");
}

function syncGammaLabel() {
  const g = Number.parseFloat(gammaCtrl.value);
  gammaVal.textContent = Number.isFinite(g) ? g.toFixed(2) : "1.00";
}

function syncGammaHint() {
  const g = Number.parseFloat(gammaCtrl.value);
  if (!Number.isFinite(g)) gammaHint.textContent = "";
  else if (g < 0.95) gammaHint.textContent = t("upload.gamma_hint_low");
  else if (g <= 1.08) gammaHint.textContent = "";
  else if (g <= 1.20) gammaHint.textContent = t("upload.gamma_hint_midhigh");
  else gammaHint.textContent = t("upload.gamma_hint_high");
}

function syncUploadOptions() {
  const mode = uploadModeSel.value || "normal";
  const isImageMode = mode === "fit" || mode === "crop";
  uploadExtra.classList.toggle("show", isImageMode);
  ditherHint.textContent = ditherAdviceText(ditherModeSel.value);
  if (mode === "normal") modeNote.textContent = t("upload.mode_note_normal");
  else if (mode === "fit") modeNote.textContent = t("upload.mode_note_fit");
  else modeNote.textContent = t("upload.mode_note_crop");
  syncGammaHint();
}

uploadModeSel.value = "normal";
ditherModeSel.value = "fs_serpentine";
uploadModeSel.addEventListener("change", syncUploadOptions);
ditherModeSel.addEventListener("change", syncUploadOptions);
gammaCtrl.addEventListener("input", syncGammaLabel);
gammaCtrl.addEventListener("input", syncGammaHint);
syncGammaLabel();
syncUploadOptions();
if (uiLanguage) {
  uiLanguage.addEventListener("change", () => {
    applyI18n(uiLanguage.value);
  });
}

function syncScheduleRepeatFields() {
  if (!scheduleRepeat || !scheduleDate || !scheduleWeekday) return;
  const mode = scheduleRepeat.value || "weekly";
  const once = mode === "once";
  const weekly = mode === "weekly";
  scheduleDate.disabled = !once;
  scheduleWeekday.disabled = !weekly;
}

if (scheduleRepeat) {
  scheduleRepeat.addEventListener("change", syncScheduleRepeatFields);
}
syncScheduleRepeatFields();

function nextStatusPollDelayMs() {
  if (document.hidden) return STATUS_POLL_MS_HIDDEN;
  return activeTabKey === "status" ? STATUS_POLL_MS_ACTIVE : STATUS_POLL_MS_IDLE;
}

function scheduleStatusPolling(delayMs) {
  if (statusPollTimer) {
    clearTimeout(statusPollTimer);
  }
  statusPollTimer = setTimeout(async () => {
    await loadStatus();
    scheduleStatusPolling(nextStatusPollDelayMs());
  }, delayMs);
}

async function ensureDataForTab(tabKey, force = false) {
  if (tabKey === "status") {
    await loadStatus();
    return;
  }
  if (tabKey === "config") {
    if (!cfgLoadedOnce || force) {
      await loadCfg();
      cfgLoadedOnce = true;
    }
    if (!schedulesLoadedOnce || force) {
      await loadSchedules();
      schedulesLoadedOnce = true;
    }
    return;
  }
  if (tabKey === "files") {
    if (!filesLoadedOnce || force) {
      await listFiles(currentDir || "/pic");
      filesLoadedOnce = true;
    }
  }
}

function setActiveTab(tabKey) {
  activeTabKey = panels[tabKey] ? tabKey : "status";
  tabs.forEach((b) => b.classList.toggle("active", b.dataset.tab === activeTabKey));
  Object.entries(panels).forEach(([name, panel]) => {
    panel.classList.toggle("active", name === activeTabKey);
  });
  void ensureDataForTab(activeTabKey);
  scheduleStatusPolling(nextStatusPollDelayMs());
}

tabs.forEach((btn) => {
  btn.addEventListener("click", () => {
    setActiveTab(btn.dataset.tab || "status");
  });
});

document.addEventListener("visibilitychange", () => {
  if (!document.hidden) {
    void loadStatus();
  }
  scheduleStatusPolling(nextStatusPollDelayMs());
});

async function loadStatus() {
  if (statusPollInFlight) {
    return;
  }
  statusPollInFlight = true;
  try {
    const r = await fetch("/api/status");
    const txt = await r.text();
    try {
      const j = JSON.parse(txt);
      latestStatusState = j.state || "";
      document.getElementById("st_mode").textContent = stateLabel(j.state || "");
      document.getElementById("st_ip").textContent = j.ip || "--";
      document.getElementById("st_apip").textContent = j.ap_ip || "--";
      document.getElementById("st_sd").textContent = j.sd_ready ? t("status.mounted") : t("status.unmounted");
      const sdTotal = Number(j.sd_total_bytes || 0);
      const sdUsed = Number(j.sd_used_bytes || 0);
      const sdFree = Math.max(0, sdTotal - sdUsed);
      document.getElementById("st_sdspace").textContent = j.sd_ready
        ? fmt("status.sd_space_fmt", {
            used: formatSize(sdUsed),
            total: formatSize(sdTotal),
            free: formatSize(sdFree),
          })
        : t("status.unmounted");
      document.getElementById("st_uptime").textContent = fmtMs(j.uptime_ms || 0);
      const tmo = j.idle_remaining_ms ?? j.connect_remaining_ms ?? 0;
      document.getElementById("st_timeout").textContent = fmtMs(tmo);
      document.getElementById("st_temp").textContent =
        (typeof j.temperature_c === "number" && j.temperature_c >= -100) ? `${j.temperature_c.toFixed(1)} °C` : t("status.not_connected");
      document.getElementById("st_humidity").textContent =
        (typeof j.humidity_pct === "number" && j.humidity_pct >= 0) ? `${j.humidity_pct.toFixed(1)} %RH` : t("status.not_connected");
      const mv = Number(j.battery_mv || 0);
      const pct = Number(j.battery_pct);
      const pin = j.battery_pin ?? "?";
      if (mv > 0 && Number.isFinite(pct) && pct > 0.1) {
        document.getElementById("st_battery").textContent = fmt("status.battery_full_fmt", {
          pct: pct.toFixed(1),
          mv,
          pin,
        });
      } else if (mv > 0) {
        document.getElementById("st_battery").textContent = fmt("status.battery_mv_fmt", { mv, pin });
      } else {
        document.getElementById("st_battery").textContent = t("status.not_connected");
      }
      setNotice("");
    } catch {
      setNotice(txt || t("common.request_failed"));
    }
  } finally {
    statusPollInFlight = false;
  }
}

async function loadCfg() {
  const r = await fetch("/api/settings");
  const j = await r.json();
  ssid.value = j.sta_ssid || "";
  pass.value = j.sta_pass || "";
  if (uiLanguage) {
    uiLanguage.value = normalizeLang(j.ui_language || "zh");
    applyI18n(uiLanguage.value);
  } else {
    applyI18n("zh");
  }
  sec.value = j.photo_interval_sec || 300;
  if (calendarLayout) {
    calendarLayout.value = j.calendar_layout || "landscape_split";
  }
  calendarSec.value = j.calendar_refresh_sec || 900;
  calendarUrl.value = j.calendar_url || "";
  weatherLocation.value = j.weather_city || "";
  weatherLat.value = j.weather_lat || "";
  weatherLon.value = j.weather_lon || "";
  wurl.value = j.weather_url || "";
  cfgLoadedOnce = true;
}

async function saveCfg() {
  try {
    let city = (weatherLocation.value || "").trim();
    let lat = (weatherLat.value || "").trim();
    let lon = (weatherLon.value || "").trim();
    let weatherUrl = (wurl.value || "").trim();

    if (city && (!lat || !lon)) {
      const ok = await resolveCity();
      if (!ok) return;
      city = (weatherLocation.value || "").trim();
      lat = (weatherLat.value || "").trim();
      lon = (weatherLon.value || "").trim();
      weatherUrl = (wurl.value || "").trim();
    }

    if (lat && lon && !weatherUrl) {
      weatherUrl = buildOpenMeteoUrl(lat, lon);
      wurl.value = weatherUrl;
    }

    const calendarLayoutValue = calendarLayout ? calendarLayout.value : "landscape_split";
    const langValue = uiLanguage ? normalizeLang(uiLanguage.value) : "zh";
    const body = `sta_ssid=${encodeURIComponent(ssid.value)}&sta_pass=${encodeURIComponent(pass.value)}&ui_language=${encodeURIComponent(langValue)}&photo_interval_sec=${encodeURIComponent(sec.value)}&calendar_enabled=1&calendar_layout=${encodeURIComponent(calendarLayoutValue)}&calendar_refresh_sec=${encodeURIComponent(calendarSec.value)}&calendar_url=${encodeURIComponent(calendarUrl.value)}&weather_city=${encodeURIComponent(city)}&weather_lat=${encodeURIComponent(lat)}&weather_lon=${encodeURIComponent(lon)}&weather_url=${encodeURIComponent(weatherUrl)}`;

    const r = await fetch("/api/settings", {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body,
    });
    const txt = await r.text();
    let j = null;
    try { j = JSON.parse(txt); } catch {}
    cfgBox.textContent = (j && j.ok) ? t("common.saved") : (txt || t("common.request_failed"));
  } catch {
    cfgBox.textContent = t("common.save_failed");
  }
}

async function testWeather() {
  if (latestStatusState !== "sta_running") {
    cfgBox.textContent = t("common.weather_sta_only");
    setNotice(t("common.not_sta_notice"));
    return;
  }
  const r = await fetch("/api/weather_test");
  const txt = await r.text();
  let j = null;
  try { j = JSON.parse(txt); } catch {}
  if (!j || !j.ok) {
    cfgBox.textContent = txt || t("common.weather_test_failed");
    return;
  }
  const tzMsg = j.timezone
    ? `${j.timezone}${j.timezone_updated ? t("common.weather_tz_updated") : ""}`
    : "--";
  const syncMsg = j.time_sync_ok
    ? fmt("common.weather_sync_ok_fmt", { time: j.local_time || "--" })
    : fmt("common.weather_sync_fail_fmt", { err: j.time_sync_error || "unknown" });
  cfgBox.textContent = fmt("common.weather_report", {
    http: j.http_status ?? "--",
    ip: j.ip || "--",
    url: j.url || "--",
    tz: tzMsg,
    sync: syncMsg,
    preview: j.preview || "",
  });
}

async function stopPortal() {
  const r = await fetch("/api/stop", { method: "POST" });
  const txt = await r.text();
  let msg = t("common.stop_sent");
  try {
    const j = JSON.parse(txt);
    if (j && j.ok) msg = j.stopping ? t("common.portal_closing") : t("common.request_done");
  } catch {}
  setNotice(msg);
}

async function rebootDevice() {
  const confirmed = window.confirm(t("common.reboot_confirm"));
  if (!confirmed) return;
  const r = await fetch("/api/reboot", { method: "POST" });
  const txt = await r.text();
  let j = null;
  try { j = JSON.parse(txt); } catch {}
  cfgBox.textContent = (j && j.ok) ? t("common.rebooting") : (txt || t("common.request_failed"));
}

function normalizeHm(text) {
  const raw = String(text || "").trim();
  const m = raw.match(/^(\d{1,2}):(\d{2})$/);
  if (!m) return "";
  const hh = Number(m[1]);
  const mm = Number(m[2]);
  if (!Number.isFinite(hh) || !Number.isFinite(mm)) return "";
  if (hh < 0 || hh > 23 || mm < 0 || mm > 59) return "";
  return `${String(hh).padStart(2, "0")}:${String(mm).padStart(2, "0")}`;
}

function normalizeRepeat(text) {
  const value = String(text || "").trim().toLowerCase();
  if (value === "once" || value === "daily" || value === "weekly") {
    return value;
  }
  return "weekly";
}

function describeEventRule(event) {
  const repeat = normalizeRepeat(event.repeat);
  if (repeat === "daily") {
    return t("repeat.daily");
  }
  if (repeat === "once") {
    return `${t("repeat.once")} ${event.date || "--"}`;
  }
  const idx = Number(event.weekday);
  const weekday = Number.isFinite(idx) && idx >= 0 && idx < 7
    ? weekdayLabel(idx)
    : "--";
  return `${t("repeat.weekly")} ${weekday}`;
}

function renderScheduleRows(items) {
  if (!scheduleRows || !scheduleSummary) return;
  lastScheduleItems = Array.isArray(items) ? items.slice() : [];
  scheduleRows.innerHTML = "";
  if (!items.length) {
    scheduleRows.innerHTML = `<tr><td colspan="5" class="small">${t("cfg.schedule.empty")}</td></tr>`;
    scheduleSummary.textContent = t("common.schedule_loaded_zero");
    return;
  }

  const colorCssMap = {
    red: "#df4d4d",
    blue: "#0f7ae5",
    green: "#1aa568",
    yellow: "#d8b21f",
    black: "#1f2a36",
    white: "#ffffff",
  };
  const repeatOrder = { daily: 0, weekly: 1, once: 2 };
  const sorted = items.slice().sort((a, b) => {
    const ra = repeatOrder[normalizeRepeat(a.repeat)] ?? 99;
    const rb = repeatOrder[normalizeRepeat(b.repeat)] ?? 99;
    if (ra !== rb) return ra - rb;
    const ta = String(a.time || "");
    const tb = String(b.time || "");
    if (ta !== tb) return ta.localeCompare(tb);
    if (normalizeRepeat(a.repeat) === "once" || normalizeRepeat(b.repeat) === "once") {
      return String(a.date || "").localeCompare(String(b.date || ""));
    }
    return Number(a.id || 0) - Number(b.id || 0);
  });

  sorted.forEach((event) => {
    const tr = document.createElement("tr");
    const colorKey = String(event.color || "blue").toLowerCase();
    const colorName = colorLabel(colorKey);
    const swatchColor = colorCssMap[colorKey] || colorCssMap.blue;
    tr.innerHTML = `
      <td>${event.time || "--:--"}</td>
      <td>${event.title || "-"}</td>
      <td>${describeEventRule(event)}</td>
      <td><span class="swatch" style="background:${swatchColor}"></span>${colorName}</td>
      <td></td>
    `;
    const del = document.createElement("button");
    del.className = "btn warn";
    del.textContent = t("common.delete");
    del.onclick = () => deleteSchedule(event.id);
    tr.children[4].appendChild(del);
    scheduleRows.appendChild(tr);
  });

  scheduleSummary.textContent = fmt("common.schedule_loaded_fmt", { count: sorted.length });
}

async function loadSchedules() {
  if (!scheduleRows || !scheduleSummary) return;
  scheduleRows.innerHTML = `<tr><td colspan="5" class="small">${t("common.loading")}</td></tr>`;
  try {
    const r = await fetch("/api/calendar/events");
    const txt = await r.text();
    let j = null;
    try { j = JSON.parse(txt); } catch {}
    if (!j || !j.ok || !Array.isArray(j.items)) {
      scheduleRows.innerHTML = `<tr><td colspan="5" class="small">${t("common.schedule_read_failed")}</td></tr>`;
      scheduleSummary.textContent = txt || t("common.schedule_read_failed");
      return;
    }
    renderScheduleRows(j.items);
    schedulesLoadedOnce = true;
  } catch {
    scheduleRows.innerHTML = `<tr><td colspan="5" class="small">${t("common.schedule_read_failed")}</td></tr>`;
    scheduleSummary.textContent = t("common.request_failed");
  }
}

async function addSchedule() {
  const title = (scheduleTitle?.value || "").trim();
  const time = normalizeHm(scheduleTime?.value || "");
  const color = (scheduleColor?.value || "blue").trim();
  const repeat = normalizeRepeat(scheduleRepeat?.value || "weekly");
  const date = (scheduleDate?.value || "").trim();
  const weekday = Number(scheduleWeekday?.value ?? 0);

  if (!title) {
    scheduleSummary.textContent = t("common.add_title_required");
    return;
  }
  if (!time) {
    scheduleSummary.textContent = t("common.bad_time");
    return;
  }
  if (repeat === "once" && !/^\d{4}-\d{2}-\d{2}$/.test(date)) {
    scheduleSummary.textContent = t("common.bad_once_date");
    return;
  }
  if (repeat === "weekly" && (!Number.isFinite(weekday) || weekday < 0 || weekday > 6)) {
    scheduleSummary.textContent = t("common.bad_weekday");
    return;
  }

  let body = `title=${encodeURIComponent(title)}&time=${encodeURIComponent(time)}&color=${encodeURIComponent(color)}&repeat=${encodeURIComponent(repeat)}`;
  if (repeat === "once") body += `&date=${encodeURIComponent(date)}`;
  if (repeat === "weekly") body += `&weekday=${encodeURIComponent(String(weekday))}`;

  try {
    const r = await fetch("/api/calendar/events", {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body,
    });
    const txt = await r.text();
    let j = null;
    try { j = JSON.parse(txt); } catch {}
    if (!j || !j.ok) {
      scheduleSummary.textContent = txt || t("common.add_failed");
      return;
    }
    if (scheduleTitle) scheduleTitle.value = "";
    await loadSchedules();
  } catch {
    scheduleSummary.textContent = t("common.add_failed_check");
  }
}

async function deleteSchedule(id) {
  const eventId = Number(id);
  if (!Number.isFinite(eventId) || eventId <= 0) return;
  const ok = window.confirm(fmt("common.del_schedule_confirm", { id: eventId }));
  if (!ok) return;
  try {
    const r = await fetch(`/api/calendar/events?id=${encodeURIComponent(String(eventId))}`, {
      method: "DELETE",
    });
    const txt = await r.text();
    let j = null;
    try { j = JSON.parse(txt); } catch {}
    if (!j || !j.ok) {
      scheduleSummary.textContent = txt || t("common.delete_failed");
      return;
    }
    await loadSchedules();
  } catch {
    scheduleSummary.textContent = t("common.del_failed_check");
  }
}

function renderBreadcrumb(path) {
  const dir = normalizeDir(path);
  crumbs.innerHTML = "";
  const parts = dir === "/" ? [] : dir.slice(1).split("/");

  const rootBtn = document.createElement("button");
  rootBtn.className = "crumb" + (dir === "/" ? " current" : "");
  rootBtn.textContent = "/";
  rootBtn.onclick = () => openDir("/");
  crumbs.appendChild(rootBtn);

  let acc = "";
  parts.forEach((part, idx) => {
    const sep = document.createElement("span");
    sep.className = "small";
    sep.textContent = "/";
    crumbs.appendChild(sep);

    acc += "/" + part;
    const btn = document.createElement("button");
    btn.className = "crumb" + (idx === parts.length - 1 ? " current" : "");
    btn.textContent = part;
    btn.onclick = () => openDir(acc);
    crumbs.appendChild(btn);
  });
}

async function openDir(path) {
  currentDir = normalizeDir(path);
  renderBreadcrumb(currentDir);
  await listFiles(currentDir);
}

async function goUpDir() {
  await openDir(parentDir(currentDir));
}

async function createFolder() {
  const name = prompt(t("common.new_folder_prompt"));
  if (!name) return;
  const folder = name.trim().replace(/[\\/]+/g, "");
  if (!folder) return;
  const path = joinPath(currentDir, folder);
  const body = `path=${encodeURIComponent(path)}`;
  const r = await fetch("/api/dir", {
    method: "POST",
    headers: { "Content-Type": "application/x-www-form-urlencoded" },
    body,
  });
  if (!r.ok) setNotice(await r.text());
  await listFiles(currentDir);
}

async function removeEntry(path, isDir) {
  const ok = window.confirm(
    isDir ? fmt("common.del_dir_confirm", { path }) : fmt("common.del_file_confirm", { path })
  );
  if (!ok) return;
  const r = await fetch("/api/file?path=" + encodeURIComponent(path), { method: "DELETE" });
  if (!r.ok) setNotice(await r.text());
  await listFiles(currentDir);
}

function renderRows(items) {
  lastFileItems = Array.isArray(items) ? items.slice() : [];
  fileRows.innerHTML = "";
  if (!items.length) {
    fileRows.innerHTML = `<tr><td colspan="4" class="small">${t("common.dir_empty")}</td></tr>`;
    return;
  }

  items.forEach((item) => {
    const name = item.name || "";
    const isDir = !!item.dir;
    const path = joinPath(currentDir, name);

    const tr = document.createElement("tr");
    const nameTd = document.createElement("td");
    const sizeTd = document.createElement("td");
    const typeTd = document.createElement("td");
    const opTd = document.createElement("td");
    opTd.className = "actions";

    if (isDir) {
      const btn = document.createElement("button");
      btn.className = "name-btn dir";
      btn.textContent = name;
      btn.onclick = () => openDir(path);
      nameTd.appendChild(btn);
    } else {
      nameTd.textContent = name;
    }

    sizeTd.textContent = isDir ? "--" : formatSize(item.size || 0);
    typeTd.innerHTML = isDir
      ? `<span class="badge dir">${t("common.folder")}</span>`
      : `<span class="badge">${t("common.file")}</span>`;

    if (isDir) {
      const openBtn = document.createElement("button");
      openBtn.className = "btn";
      openBtn.textContent = t("common.open");
      openBtn.onclick = () => openDir(path);

      const delBtn = document.createElement("button");
      delBtn.className = "btn warn";
      delBtn.textContent = t("common.delete");
      delBtn.onclick = () => removeEntry(path, true);

      opTd.appendChild(openBtn);
      opTd.appendChild(delBtn);
    } else {
      const dl = document.createElement("a");
      dl.className = "btn";
      dl.textContent = t("common.download");
      dl.href = "/api/file?path=" + encodeURIComponent(path);
      dl.target = "_blank";

      const delBtn = document.createElement("button");
      delBtn.className = "btn warn";
      delBtn.textContent = t("common.delete");
      delBtn.onclick = () => removeEntry(path, false);

      opTd.appendChild(dl);
      opTd.appendChild(delBtn);
    }

    tr.appendChild(nameTd);
    tr.appendChild(sizeTd);
    tr.appendChild(typeTd);
    tr.appendChild(opTd);
    fileRows.appendChild(tr);
  });
}

async function listFiles(path) {
  currentDir = normalizeDir(path || currentDir || "/pic");
  renderBreadcrumb(currentDir);
  fileRows.innerHTML = `<tr><td colspan="4" class="small">${t("common.loading")}</td></tr>`;

  const r = await fetch("/api/files?path=" + encodeURIComponent(currentDir));
  const txt = await r.text();
  let j = null;
  try { j = JSON.parse(txt); } catch {}
  if (!j || !j.ok) {
    fileRows.innerHTML = `<tr><td colspan="4" class="small">${t("common.dir_read_failed")}</td></tr>`;
    fileSummary.textContent = txt || t("common.dir_read_failed");
    setNotice(txt || t("common.dir_read_failed"));
    return;
  }

  const items = Array.isArray(j.items) ? j.items.slice() : [];
  const locale = currentLanguage === "fr" ? "fr" : (currentLanguage === "en" ? "en" : "zh-CN");
  items.sort((a, b) => {
    if (!!a.dir !== !!b.dir) return a.dir ? -1 : 1;
    return String(a.name || "").localeCompare(String(b.name || ""), locale, {
      numeric: true,
      sensitivity: "base",
    });
  });

  renderRows(items);
  fileSummary.textContent = fmt("common.current_dir_fmt", { dir: currentDir, count: items.length });
  setNotice("");
  filesLoadedOnce = true;
}

function clamp255(v) {
  return v < 0 ? 0 : (v > 255 ? 255 : v);
}

function threshold(v) {
  return v < 128 ? 0 : 255;
}

function quantizeImageToEpd4(rgba, W, H, palette, ditherMode = "fs_serpentine", gammaValue = 1.0) {
  const n = W * H;
  const rr = new Float32Array(n);
  const gg = new Float32Array(n);
  const bb = new Float32Array(n);
  const pix = new Uint8Array(n);

  const gamma = Number.isFinite(gammaValue) && gammaValue > 0 ? gammaValue : 1.0;

  for (let i = 0, p = 0; i < n; i++, p += 4) {
    const a = rgba[p + 3] / 255;
    const r = (rgba[p] / 255) * a + (1 - a);
    const g = (rgba[p + 1] / 255) * a + (1 - a);
    const b = (rgba[p + 2] / 255) * a + (1 - a);
    rr[i] = clamp255(Math.round(Math.pow(r, gamma) * 255));
    gg[i] = clamp255(Math.round(Math.pow(g, gamma) * 255));
    bb[i] = clamp255(Math.round(Math.pow(b, gamma) * 255));
  }

  function nearest(r, g, b) {
    let best = 0;
    let bestD = Infinity;
    for (let k = 0; k < palette.length; k++) {
      const pr = palette[k].rgb[0], pg = palette[k].rgb[1], pb = palette[k].rgb[2];
      const dr = r - pr, dg = g - pg, db = b - pb;
      const d = dr * dr + dg * dg + db * db;
      if (d < bestD) { bestD = d; best = k; }
    }
    return best;
  }

  function addErr(x, y, er, eg, eb, f) {
    if (x < 0 || x >= W || y < 0 || y >= H) return;
    const i = y * W + x;
    rr[i] = clamp255(rr[i] + er * f);
    gg[i] = clamp255(gg[i] + eg * f);
    bb[i] = clamp255(bb[i] + eb * f);
  }

  const mode = (ditherMode || "fs_serpentine").toLowerCase();

  for (let y = 0; y < H; y++) {
    const reverse = (mode === "fs_serpentine" || mode === "jjn") && (y & 1);
    const xs = reverse ? W - 1 : 0;
    const xe = reverse ? -1 : W;
    const st = reverse ? -1 : 1;

    for (let x = xs; x !== xe; x += st) {
      const i = y * W + x;
      const oldR = rr[i], oldG = gg[i], oldB = bb[i];
      const k = nearest(oldR, oldG, oldB);
      pix[i] = palette[k].nib;
      const nr = palette[k].rgb[0], ng = palette[k].rgb[1], nb = palette[k].rgb[2];
      const er = oldR - nr, eg = oldG - ng, eb = oldB - nb;

      if (mode === "fs_serpentine") {
        if (!reverse) {
          addErr(x + 1, y, er, eg, eb, 7 / 16);
          addErr(x - 1, y + 1, er, eg, eb, 3 / 16);
          addErr(x, y + 1, er, eg, eb, 5 / 16);
          addErr(x + 1, y + 1, er, eg, eb, 1 / 16);
        } else {
          addErr(x - 1, y, er, eg, eb, 7 / 16);
          addErr(x + 1, y + 1, er, eg, eb, 3 / 16);
          addErr(x, y + 1, er, eg, eb, 5 / 16);
          addErr(x - 1, y + 1, er, eg, eb, 1 / 16);
        }
      } else if (mode === "atkinson") {
        const f = 1 / 8;
        addErr(x + 1, y, er, eg, eb, f);
        addErr(x + 2, y, er, eg, eb, f);
        addErr(x - 1, y + 1, er, eg, eb, f);
        addErr(x, y + 1, er, eg, eb, f);
        addErr(x + 1, y + 1, er, eg, eb, f);
        addErr(x, y + 2, er, eg, eb, f);
      } else if (mode === "jjn") {
        if (!reverse) {
          addErr(x + 1, y, er, eg, eb, 7 / 48); addErr(x + 2, y, er, eg, eb, 5 / 48);
          addErr(x - 2, y + 1, er, eg, eb, 3 / 48); addErr(x - 1, y + 1, er, eg, eb, 5 / 48);
          addErr(x, y + 1, er, eg, eb, 7 / 48); addErr(x + 1, y + 1, er, eg, eb, 5 / 48); addErr(x + 2, y + 1, er, eg, eb, 3 / 48);
          addErr(x - 2, y + 2, er, eg, eb, 1 / 48); addErr(x - 1, y + 2, er, eg, eb, 3 / 48);
          addErr(x, y + 2, er, eg, eb, 5 / 48); addErr(x + 1, y + 2, er, eg, eb, 3 / 48); addErr(x + 2, y + 2, er, eg, eb, 1 / 48);
        } else {
          addErr(x - 1, y, er, eg, eb, 7 / 48); addErr(x - 2, y, er, eg, eb, 5 / 48);
          addErr(x + 2, y + 1, er, eg, eb, 3 / 48); addErr(x + 1, y + 1, er, eg, eb, 5 / 48);
          addErr(x, y + 1, er, eg, eb, 7 / 48); addErr(x - 1, y + 1, er, eg, eb, 5 / 48); addErr(x - 2, y + 1, er, eg, eb, 3 / 48);
          addErr(x + 2, y + 2, er, eg, eb, 1 / 48); addErr(x + 1, y + 2, er, eg, eb, 3 / 48);
          addErr(x, y + 2, er, eg, eb, 5 / 48); addErr(x - 1, y + 2, er, eg, eb, 3 / 48); addErr(x - 2, y + 2, er, eg, eb, 1 / 48);
        }
      }
    }
  }

  const out = new Uint8Array((W * H) >> 1);
  let oi = 0;
  for (let i = 0; i < W * H; i += 2) {
    out[oi++] = ((pix[i] & 0x0F) << 4) | (pix[i + 1] & 0x0F);
  }
  return out;
}

async function preprocessImageToEpd4Blob(file, cropMode, ditherMode = "fs_serpentine", gammaValue = 1.0) {
  const img = new Image();
  const dataUrl = await new Promise((resolve, reject) => {
    const fr = new FileReader();
    fr.onload = () => resolve(fr.result);
    fr.onerror = () => reject(new Error("read_failed"));
    fr.readAsDataURL(file);
  });

  await new Promise((resolve, reject) => {
    img.onload = () => resolve();
    img.onerror = () => reject(new Error("decode_failed"));
    img.src = dataUrl;
  });

  const W = 800;
  const H = 480;
  const canvas = document.createElement("canvas");
  canvas.width = W;
  canvas.height = H;
  const ctx = canvas.getContext("2d", { willReadFrequently: true });

  const srcLandscape = img.width >= img.height;
  const frameLandscape = W >= H;
  const rotate90 = srcLandscape !== frameLandscape;
  const srcW = rotate90 ? img.height : img.width;
  const srcH = rotate90 ? img.width : img.height;
  const sx = W / srcW;
  const sy = H / srcH;
  const scale = cropMode ? Math.max(sx, sy) : Math.min(sx, sy);
  const dw = srcW * scale;
  const dh = srcH * scale;

  ctx.fillStyle = "#ffffff";
  ctx.fillRect(0, 0, W, H);

  if (!rotate90) {
    ctx.drawImage(img, (W - dw) * 0.5, (H - dh) * 0.5, dw, dh);
  } else {
    ctx.save();
    ctx.translate(W * 0.5, H * 0.5);
    ctx.rotate(-Math.PI / 2);
    ctx.drawImage(img, -dh * 0.5, -dw * 0.5, dh, dw);
    ctx.restore();
  }

  const rgba = ctx.getImageData(0, 0, W, H).data;
  const palette = [
    { rgb: [0, 0, 0], nib: 0x00 },
    { rgb: [255, 255, 255], nib: 0x01 },
    { rgb: [255, 255, 0], nib: 0x02 },
    { rgb: [255, 0, 0], nib: 0x03 },
    { rgb: [0, 0, 255], nib: 0x05 },
    { rgb: [0, 255, 0], nib: 0x06 },
  ];

  const epd4 = quantizeImageToEpd4(rgba, W, H, palette, ditherMode, gammaValue);
  return new Blob([epd4], { type: "application/octet-stream" });
}

async function uploadFile() {
  const fileEl = document.getElementById("uploadInput");
  const f = fileEl.files && fileEl.files[0];
  if (!f) {
    uploadBox.textContent = t("upload.pick_file");
    return;
  }

  const mode = (uploadModeSel.value || "normal").toLowerCase();
  const ditherMode = ditherModeSel.value || "fs_serpentine";
  const gammaValue = Number.parseFloat(gammaCtrl.value || "1.0");
  const gammaText = Number.isFinite(gammaValue) ? gammaValue.toFixed(2) : "1.00";

  if (mode === "normal") {
    if (uploadBtn) uploadBtn.disabled = true;
    const fd = new FormData();
    fd.append("file", f);
    const q = "/api/upload?dir=" + encodeURIComponent(currentDir) + "&mode=normal&algo=" + encodeURIComponent(ditherMode) + "&gamma=" + encodeURIComponent(gammaText);
    const r = await fetch(q, { method: "POST", body: fd });
    uploadBox.textContent = await r.text();
    await listFiles(currentDir);
    if (uploadBtn) uploadBtn.disabled = false;
    return;
  }

  const lower = (f.name || "").toLowerCase();
  if (!(lower.endsWith(".jpg") || lower.endsWith(".jpeg") || lower.endsWith(".png") || lower.endsWith(".bmp"))) {
    uploadBox.textContent = t("upload.only_images");
    return;
  }

  uploadBox.textContent = t("upload.preprocessing");
  if (uploadBtn) uploadBtn.disabled = true;
  try {
    const epdBlob = await preprocessImageToEpd4Blob(f, mode === "crop", ditherMode, gammaValue);
    const outName = (f.name.replace(/\.[^.]+$/, "") || "image") + ".epd4";
    const fd = new FormData();
    fd.append("file", epdBlob, outName);
    const q = "/api/upload?dir=" + encodeURIComponent("/pic") + "&mode=normal&algo=" + encodeURIComponent(ditherMode) + "&gamma=" + encodeURIComponent(gammaText);
    const r = await fetch(q, { method: "POST", body: fd });
    uploadBox.textContent = await r.text();
    currentDir = "/pic";
    await listFiles("/pic");
  } catch (e) {
    uploadBox.textContent = fmt("upload.preprocess_failed", {
      err: (e && e.message ? e.message : String(e)),
    });
  } finally {
    if (uploadBtn) uploadBtn.disabled = false;
  }
}

applyI18n(normalizeLang(uiLanguage ? uiLanguage.value : "zh"));
setActiveTab("status");
