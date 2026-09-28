import 'package:flutter/material.dart';
import '../theme/app_theme.dart';

class HiveData {
  final String id;
  final String name;
  final String deviceId;
  final String notes;
  final String conditionLabel;
  final int confidence;
  final int healthScore;
  final String temperature;
  final String humidity;
  final String acoustic;
  final String acousticStatus;
  final String wifiStatus;
  final String batteryLevel;
  final String updated;
  final int signalBars;

  final String explanation;
  final bool queenPresentDetected;
  final bool queenAbsentDetected;
  final bool queenAcceptedDetected;
  final bool queenRejectedDetected;
  final String recommendation;

  final List<String> historyDates;
  final List<double> temperatureHistory;
  final List<double> humidityHistory;
  final List<double> acousticHistory;
  final List<Map<String, String>> conditionTimeline;

  final bool isAlert;
  final String alertSeverity; // Critical, Warning, Info
  final String alertLabel;
  final String alertMessage;
  final String alertTime;
  final String detectedBy;
  final String alertRecommendation;
  final String? audioFilePath;

  HiveData({
    required this.id,
    required this.name,
    this.deviceId = 'BW-001-A',
    this.notes = 'Main Apiary hive',
    required this.conditionLabel,
    required this.confidence,
    required this.healthScore,
    required this.temperature,
    required this.humidity,
    required this.acoustic,
    this.acousticStatus = 'Normal',
    this.wifiStatus = 'Connected',
    this.batteryLevel = '90%',
    required this.updated,
    this.signalBars = 4,
    this.explanation =
        'The AI analyzed the hive\'s acoustic, temperature, and humidity data and classified the colony state.',
    this.queenPresentDetected = true,
    this.queenAbsentDetected = false,
    this.queenAcceptedDetected = false,
    this.queenRejectedDetected = false,
    this.recommendation =
        'Continue routine monitoring. No intervention required.',
    this.historyDates = const [],
    this.temperatureHistory = const [],
    this.humidityHistory = const [],
    this.acousticHistory = const [],
    this.conditionTimeline = const [],
    required this.isAlert,
    this.alertSeverity = 'Info',
    this.alertLabel = 'Normal',
    required this.alertMessage,
    this.alertTime = 'Just now',
    this.detectedBy = 'AI Acoustic Analysis',
    this.alertRecommendation = 'Continue regular inspection routine.',
    this.audioFilePath,
  });

  Color get labelColor {
    final label = conditionLabel.toLowerCase();
    if (label.contains('present')) return AppColors.queenPresentGreen;
    if (label.contains('absent')) return AppColors.queenAbsentRed;
    if (label.contains('accepted')) return AppColors.queenAcceptedBlue;
    if (label.contains('rejected')) return AppColors.queenRejectedOrange;
    if (label.contains('healthy')) return AppColors.healthyGreen;
    return Colors.black87;
  }

  Color get labelBgColor {
    final label = conditionLabel.toLowerCase();
    if (label.contains('present')) return AppColors.queenPresentGreenBg;
    if (label.contains('absent')) return AppColors.queenAbsentRedBg;
    if (label.contains('accepted')) return AppColors.queenAcceptedBlueBg;
    if (label.contains('rejected')) return AppColors.queenRejectedOrangeBg;
    if (label.contains('healthy')) return AppColors.healthyGreenBg;
    return const Color(0xFFF0F0F0);
  }

  bool get isSensorOffline {
    final wifi = wifiStatus.toLowerCase();
    if (wifi.contains('disconnect') || wifi.contains('offline')) return true;
    final up = updated.toLowerCase();
    if (up.contains('hr') || up.contains('hour') || up.contains('day') || up.contains('offline')) {
      return true;
    }
    return false;
  }

  String get lastSeenText {
    if (updated.toLowerCase().contains('just now')) return 'Live';
    return 'Last seen: $updated';
  }

  HiveData copyWith({
    String? id,
    String? name,
    String? deviceId,
    String? notes,
    String? conditionLabel,
    int? confidence,
    int? healthScore,
    String? temperature,
    String? humidity,
    String? acoustic,
    String? acousticStatus,
    String? wifiStatus,
    String? batteryLevel,
    String? updated,
    int? signalBars,
    String? explanation,
    bool? queenPresentDetected,
    bool? queenAbsentDetected,
    bool? queenAcceptedDetected,
    bool? queenRejectedDetected,
    String? recommendation,
    List<String>? historyDates,
    List<double>? temperatureHistory,
    List<double>? humidityHistory,
    List<double>? acousticHistory,
    List<Map<String, String>>? conditionTimeline,
    bool? isAlert,
    String? alertSeverity,
    String? alertLabel,
    String? alertMessage,
    String? alertTime,
    String? detectedBy,
    String? alertRecommendation,
    String? audioFilePath,
  }) {
    return HiveData(
      id: id ?? this.id,
      name: name ?? this.name,
      deviceId: deviceId ?? this.deviceId,
      notes: notes ?? this.notes,
      conditionLabel: conditionLabel ?? this.conditionLabel,
      confidence: confidence ?? this.confidence,
      healthScore: healthScore ?? this.healthScore,
      temperature: temperature ?? this.temperature,
      humidity: humidity ?? this.humidity,
      acoustic: acoustic ?? this.acoustic,
      acousticStatus: acousticStatus ?? this.acousticStatus,
      wifiStatus: wifiStatus ?? this.wifiStatus,
      batteryLevel: batteryLevel ?? this.batteryLevel,
      updated: updated ?? this.updated,
      signalBars: signalBars ?? this.signalBars,
      explanation: explanation ?? this.explanation,
      queenPresentDetected: queenPresentDetected ?? this.queenPresentDetected,
      queenAbsentDetected: queenAbsentDetected ?? this.queenAbsentDetected,
      queenAcceptedDetected: queenAcceptedDetected ?? this.queenAcceptedDetected,
      queenRejectedDetected: queenRejectedDetected ?? this.queenRejectedDetected,
      recommendation: recommendation ?? this.recommendation,
      historyDates: historyDates ?? this.historyDates,
      temperatureHistory: temperatureHistory ?? this.temperatureHistory,
      humidityHistory: humidityHistory ?? this.humidityHistory,
      acousticHistory: acousticHistory ?? this.acousticHistory,
      conditionTimeline: conditionTimeline ?? this.conditionTimeline,
      isAlert: isAlert ?? this.isAlert,
      alertSeverity: alertSeverity ?? this.alertSeverity,
      alertLabel: alertLabel ?? this.alertLabel,
      alertMessage: alertMessage ?? this.alertMessage,
      alertTime: alertTime ?? this.alertTime,
      detectedBy: detectedBy ?? this.detectedBy,
      alertRecommendation: alertRecommendation ?? this.alertRecommendation,
      audioFilePath: audioFilePath ?? this.audioFilePath,
    );
  }

  factory HiveData.fromFirestore(String id, Map<String, dynamic> data) {
    final rawFreq = data['frequency'] ?? data['frequency_hz'];
    int parsedFreq = 0;
    if (rawFreq is num) {
      parsedFreq = rawFreq.toInt();
    } else if (rawFreq is String) {
      parsedFreq = int.tryParse(rawFreq.replaceAll(RegExp(r'[^0-9]'), '')) ?? 0;
    }

    final rawAcoustic = (data['acoustic'] ?? (parsedFreq > 0 ? '$parsedFreq Hz' : '0 Hz')).toString();
    final acousticClean = rawAcoustic.replaceAll(RegExp(r'[^0-9]'), '').trim();
    final int freqHz = parsedFreq > 0 ? parsedFreq : (int.tryParse(acousticClean) ?? 0);

    final bool isAcousticDetected = freqHz > 0 &&
        !rawAcoustic.toLowerCase().contains('not detected') &&
        !(data['acousticStatus']?.toString().toLowerCase().contains('not detected') ?? false);

    final String condition;
    final bool isPresent;
    final bool isAbsent;
    final bool isAccepted;
    final bool isRejected;

    if (!isAcousticDetected) {
      condition = 'No Buzz Detected';
      isPresent = false;
      isAbsent = false;
      isAccepted = false;
      isRejected = false;
    } else {
      condition = data['conditionLabel'] ?? (freqHz > 260 ? 'Queen Absent' : 'Queen Present');
      isAbsent = condition.toLowerCase().contains('absent');
      isRejected = condition.toLowerCase().contains('rejected');
      isAccepted = condition.toLowerCase().contains('accepted');
      isPresent = !isAbsent && !isRejected && !isAccepted;
    }

    List<double> parseDoubleList(dynamic list, List<double> fallback) {
      if (list is List) {
        return list.map((e) => (e as num).toDouble()).toList();
      }
      return fallback;
    }

    return HiveData(
      id: id,
      name: data['name'] ?? 'Hive',
      deviceId: data['deviceId'] ?? 'BW-001',
      notes: data['notes'] ?? '',
      conditionLabel: condition,
      confidence: !isAcousticDetected ? 0 : (((data['confidence'] as num?)?.toInt() ?? 0) > 0 ? (data['confidence'] as num)!.toInt() : 94),
      healthScore: !isAcousticDetected ? 0 : (((data['healthScore'] as num?)?.toInt() ?? 0) > 0 ? (data['healthScore'] as num)!.toInt() : 92),
      temperature: data['temperature']?.toString() ?? '34.0',
      humidity: data['humidity']?.toString() ?? '60',
      acoustic: !isAcousticDetected ? '0 Hz' : (freqHz > 0 ? '$freqHz Hz' : rawAcoustic),
      acousticStatus: !isAcousticDetected ? 'Not Detected (0 Hz)' : (data['acousticStatus'] ?? 'Normal'),
      wifiStatus: data['wifiStatus'] ?? 'Connected',
      batteryLevel: data['batteryLevel'] ?? '90%',
      updated: data['updated'] ?? 'Just now',
      signalBars: (data['signalBars'] as num?)?.toInt() ?? 4,
      explanation: !isAcousticDetected
          ? 'No bee buzz detected (0 Hz / Silence). Ensure the microphone is connected and placed near the hive cluster.'
          : (data['explanation'] ??
              'The AI analyzed the hive\'s acoustic, temperature, and humidity data and classified the colony state.'),
      queenPresentDetected: isPresent,
      queenAbsentDetected: isAbsent,
      queenAcceptedDetected: isAccepted,
      queenRejectedDetected: isRejected,
      recommendation: !isAcousticDetected
          ? 'Awaiting acoustic signal from colony. Routine monitoring active.'
          : (data['recommendation'] ??
              (isAbsent
                  ? 'Inspect frames for emergency queen cells.'
                  : (isRejected
                      ? 'Check release cage and examine worker agitation.'
                      : 'Colony is queenright and stable. Continue regular monitoring.'))),
      historyDates: data['historyDates'] != null
          ? List<String>.from(data['historyDates'])
          : const [],
      temperatureHistory: parseDoubleList(data['temperatureHistory'], const []),
      humidityHistory: parseDoubleList(data['humidityHistory'], const []),
      acousticHistory: parseDoubleList(data['acousticHistory'], const []),
      isAlert: !isAcousticDetected ? false : (data['isAlert'] ?? (isAbsent || isRejected)),
      alertSeverity: !isAcousticDetected ? 'Info' : (data['alertSeverity'] ?? (isAbsent ? 'Critical' : (isRejected ? 'Warning' : 'Info'))),
      alertLabel: condition,
      alertMessage: !isAcousticDetected
          ? 'No Buzz Detected'
          : (data['alertMessage'] ??
              (isAbsent
                  ? 'Colony is Queenless.'
                  : (isRejected
                      ? 'Colony rejecting queen.'
                      : 'Colony is stable.'))),
      alertTime: data['alertTime'] ?? 'Just now',
      detectedBy: data['detectedBy'] ?? 'ESP32 & AI Acoustic Model',
      alertRecommendation: !isAcousticDetected
          ? 'Ensure microphone is connected.'
          : (data['alertRecommendation'] ??
              (isAbsent
                  ? 'Inspect frames for emergency queen cells.'
                  : (isRejected
                      ? 'Check release cage and examine worker agitation.'
                      : 'Continue regular inspection routine.'))),
      audioFilePath: data['audioFilePath'] ?? data['audio_file_path'],
    );
  }

  Map<String, dynamic> toMap() {
    return {
      'name': name,
      'deviceId': deviceId,
      'notes': notes,
      'conditionLabel': conditionLabel,
      'confidence': confidence,
      'healthScore': healthScore,
      'temperature': temperature,
      'humidity': humidity,
      'acoustic': acoustic,
      'acousticStatus': acousticStatus,
      'wifiStatus': wifiStatus,
      'batteryLevel': batteryLevel,
      'updated': updated,
      'signalBars': signalBars,
      'explanation': explanation,
      'queenPresentDetected': queenPresentDetected,
      'queenAbsentDetected': queenAbsentDetected,
      'queenAcceptedDetected': queenAcceptedDetected,
      'queenRejectedDetected': queenRejectedDetected,
      'recommendation': recommendation,
      'temperatureHistory': temperatureHistory,
      'humidityHistory': humidityHistory,
      'acousticHistory': acousticHistory,
      'isAlert': isAlert,
      'alertSeverity': alertSeverity,
      'alertLabel': alertLabel,
      'alertMessage': alertMessage,
      'alertTime': alertTime,
      'detectedBy': detectedBy,
      'alertRecommendation': alertRecommendation,
      'audioFilePath': audioFilePath,
    };
  }

  Map<String, dynamic> toJson() => {
        'id': id,
        ...toMap(),
      };

  factory HiveData.fromJson(Map<String, dynamic> json) {
    return HiveData.fromFirestore(json['id'] ?? '', json);
  }

  static List<HiveData> samples = [
    HiveData(
      id: 'hive_1',
      name: 'Hive 1',
      deviceId: 'BW-001-ALPHA',
      notes: 'South garden station',
      conditionLabel: 'No Buzz Detected',
      confidence: 0,
      healthScore: 0,
      temperature: '34.2',
      humidity: '64',
      acoustic: '0 Hz',
      acousticStatus: 'Not Detected (0 Hz)',
      wifiStatus: 'Connected',
      batteryLevel: '95%',
      updated: 'Just Now',
      signalBars: 4,
      explanation:
          'No bee buzz detected (0 Hz / Silence). Ensure the microphone is connected and placed near the hive cluster.',
      queenPresentDetected: false,
      queenAbsentDetected: false,
      queenAcceptedDetected: false,
      queenRejectedDetected: false,
      recommendation:
          'Awaiting acoustic signal from colony. Routine monitoring active.',
      isAlert: false,
      alertSeverity: 'Info',
      alertLabel: 'No Buzz Detected',
      alertMessage: 'Awaiting acoustic signal from colony.',
      alertTime: 'Just now',
      detectedBy: 'AI Multi-Sensor Acoustic Model',
      alertRecommendation: 'Ensure microphone is connected.',
    ),
    HiveData(
      id: 'hive_2',
      name: 'Hive 2',
      deviceId: 'BW-002-BETA',
      notes: 'East apiary corner',
      conditionLabel: 'Queen Accepted',
      confidence: 88,
      healthScore: 88,
      temperature: '34.8',
      humidity: '62',
      acoustic: '240 Hz',
      acousticStatus: 'Normal',
      wifiStatus: 'Connected',
      batteryLevel: '85%',
      updated: '2 mins ago',
      signalBars: 4,
      explanation:
          'Acoustic frequencies (240 Hz) and worker hum indicate that the newly introduced queen was successfully accepted.',
      queenPresentDetected: false,
      queenAbsentDetected: false,
      queenAcceptedDetected: true,
      queenRejectedDetected: false,
      recommendation:
          'Queen accepted. Avoid disturbing brood box for 5 days while egg laying stabilizes.',
      isAlert: false,
      alertSeverity: 'Info',
      alertLabel: 'Queen Accepted',
      alertMessage: 'Colony has successfully accepted the introduced queen.',
      alertTime: '12 mins ago',
      detectedBy: 'AI Acoustic Classifier',
      alertRecommendation:
          'Check for newly laid eggs in 5 days.',
    ),
    HiveData(
      id: 'hive_3',
      name: 'Hive 3',
      deviceId: 'BW-003-GAMMA',
      notes: 'Main breeding colony',
      conditionLabel: 'Queen Absent',
      confidence: 58,
      healthScore: 45,
      temperature: '32.1',
      humidity: '55',
      acoustic: '380 Hz',
      acousticStatus: 'Abnormal',
      wifiStatus: 'Connected',
      batteryLevel: '78%',
      updated: '1 min ago',
      signalBars: 3,
      explanation:
          'Acoustic signature shows characteristic queenless roar (380 Hz) and absence of queen piping signals.',
      queenPresentDetected: false,
      queenAbsentDetected: true,
      queenAcceptedDetected: false,
      queenRejectedDetected: false,
      recommendation:
          'Inspect frames for emergency queen cells or introduce a new mated queen promptly.',
      isAlert: true,
      alertSeverity: 'Critical',
      alertLabel: 'Queen Absent',
      alertMessage: 'Acoustic signals indicate that the hive is Queenless.',
      alertTime: '2 mins ago',
      detectedBy: 'AI Acoustic Model',
      alertRecommendation:
          'Inspect frames for emergency queen cells or introduce a new queen.',
    ),
    HiveData(
      id: 'hive_4',
      name: 'Hive 4',
      deviceId: 'BW-004-DELTA',
      notes: 'New split colony',
      conditionLabel: 'Queen Rejected',
      confidence: 41,
      healthScore: 35,
      temperature: '37.5',
      humidity: '58',
      acoustic: '420 Hz',
      acousticStatus: 'High Distress',
      wifiStatus: 'Connected',
      batteryLevel: '92%',
      updated: '30 mins ago',
      signalBars: 4,
      explanation:
          'High agitation buzzing and localized thermal spikes suggest workers are rejecting or balling the queen.',
      queenPresentDetected: false,
      queenAbsentDetected: false,
      queenAcceptedDetected: false,
      queenRejectedDetected: true,
      recommendation:
          'Inspect the release cage immediately, check for worker aggression, and consider slow-release method.',
      isAlert: true,
      alertSeverity: 'Warning',
      alertLabel: 'Queen Rejected',
      alertMessage: 'Colony is rejecting the introduced queen.',
      alertTime: '30 mins ago',
      detectedBy: 'AI Acoustic & Thermal Analysis',
      alertRecommendation:
          'Check release cage and release method to prevent queen injury.',
    ),
  ];
}
