import 'dart:async';
import 'dart:convert';
import 'package:cloud_firestore/cloud_firestore.dart';
import 'package:flutter/foundation.dart';
import 'package:shared_preferences/shared_preferences.dart';
import '../models/hive_data.dart';
import 'auth_service.dart';
import 'connectivity_service.dart';

class HiveService extends ChangeNotifier {
  static final HiveService _instance = HiveService._internal();

  factory HiveService() => _instance;

  HiveService._internal() {
    _hives = [];
    _loadFromCache();
    _listenToAuthChanges();
  }

  List<HiveData> _hives = [];
  StreamSubscription<QuerySnapshot>? _hivesSubscription;
  StreamSubscription? _authSubscription;
  Timer? _debounceTimer;

  List<HiveData> get hives => List.unmodifiable(_hives);

  void _debouncedNotify() {
    _debounceTimer?.cancel();
    _debounceTimer = Timer(const Duration(milliseconds: 300), () {
      notifyListeners();
    });
  }

  Future<void> _saveToCache() async {
    try {
      final prefs = await SharedPreferences.getInstance();
      final jsonList = _hives.map((h) => h.toJson()).toList();
      final encoded = jsonEncode(jsonList);
      await prefs.setString('beeware_cached_shared_apiary_hives', encoded);
      await prefs.setString('beeware_cached_hives_latest', encoded);
    } catch (e) {
      debugPrint('Error saving hives to cache: $e');
    }
  }

  Future<void> _loadFromCache() async {
    try {
      final prefs = await SharedPreferences.getInstance();
      String? raw = prefs.getString('beeware_cached_shared_apiary_hives');
      raw ??= prefs.getString('beeware_cached_hives_latest');

      if (raw != null && raw.isNotEmpty) {
        final List<dynamic> decoded = jsonDecode(raw);
        final cached = decoded
            .map((item) => HiveData.fromJson(Map<String, dynamic>.from(item as Map)))
            .toList();
        _hives = cached;
        notifyListeners();
        return;
      }
    } catch (e) {
      debugPrint('Error loading cached hives: $e');
    }
  }

  void _listenToAuthChanges() {
    try {
      // Connect to Firestore stream immediately on app startup
      _initFirestoreStream();

      _authSubscription = AuthService().authStateChanges().listen((user) {
        // Ensure stream is active and refresh from cloud
        _initFirestoreStream();
        refreshFromCloud();
      });
    } catch (e) {
      debugPrint('Auth listener init skipped: $e');
    }
  }

  void _initFirestoreStream() {
    _hivesSubscription?.cancel();
    try {
      _hivesSubscription = FirebaseFirestore.instance
          .collection('hives')
          .snapshots()
          .listen((snapshot) {
        if (snapshot.docs.isNotEmpty) {
          _hives = snapshot.docs.map((doc) {
            return HiveData.fromFirestore(doc.id, doc.data());
          }).toList();
        } else {
          // If Firestore collection is empty, keep it empty for clean public use
          _hives = [];
        }

        _saveToCache();
        ConnectivityService().recordSyncEvent();
        _debouncedNotify();
      }, onError: (e) {
        debugPrint('Firestore shared hives stream error: $e');
      });
    } catch (e) {
      debugPrint('Firestore stream init skipped: $e');
    }
  }

  /// Manually trigger a fresh cloud fetch (e.g. pull to refresh or reconnection)
  Future<void> refreshFromCloud() async {
    try {
      final snapshot = await FirebaseFirestore.instance
          .collection('hives')
          .get()
          .timeout(const Duration(seconds: 6));

      if (snapshot.docs.isNotEmpty) {
        _hives = snapshot.docs.map((doc) {
          return HiveData.fromFirestore(doc.id, doc.data());
        }).toList();

        _saveToCache();
        ConnectivityService().recordSyncEvent();
        notifyListeners();
      } else {
        _hives = [];
        _saveToCache();
        ConnectivityService().recordSyncEvent();
        notifyListeners();
      }
    } catch (e) {
      debugPrint('Cloud refresh skipped or offline: $e');
    }
  }

  void addHive(HiveData hive) {
    // Add locally for instant responsive UI
    _hives.removeWhere((h) => h.id == hive.id);
    _hives.add(hive);
    _saveToCache();
    notifyListeners();

    // Push to shared Firestore collection so all users receive it in real-time
    try {
      FirebaseFirestore.instance.collection('hives').doc(hive.id).set({
        ...hive.toMap(),
        'updatedAt': FieldValue.serverTimestamp(),
      }, SetOptions(merge: true)).catchError((e) {
        debugPrint('Firestore add shared hive error: $e');
      });
    } catch (e) {
      debugPrint('Firestore add shared hive skipped: $e');
    }
  }

  void updateHive(HiveData hive) {
    final index = _hives.indexWhere((h) => h.id == hive.id);
    if (index != -1) {
      _hives[index] = hive;
      _saveToCache();
      notifyListeners();
    }

    // Push update to shared Firestore collection
    try {
      FirebaseFirestore.instance.collection('hives').doc(hive.id).set({
        ...hive.toMap(),
        'updatedAt': FieldValue.serverTimestamp(),
      }, SetOptions(merge: true)).catchError((e) {
        debugPrint('Firestore update shared hive error: $e');
      });
    } catch (e) {
      debugPrint('Firestore update shared hive skipped: $e');
    }
  }

  void deleteHive(String id) {
    _hives.removeWhere((h) => h.id == id);
    _saveToCache();
    notifyListeners();

    // Delete from shared Firestore collection so it reflects to all users immediately
    try {
      FirebaseFirestore.instance.collection('hives').doc(id).delete().catchError((e) {
        debugPrint('Firestore delete shared hive error: $e');
      });
    } catch (e) {
      debugPrint('Firestore delete shared hive skipped: $e');
    }
  }

  HiveData? getHiveById(String id) {
    try {
      return _hives.firstWhere((h) => h.id == id);
    } catch (_) {
      return null;
    }
  }

  /// Updates local hive instances with fresh SQLite telemetry data from FastAPI backend
  void updateFromBackendTelemetry(List<Map<String, dynamic>> records) {
    if (records.isEmpty) return;

    // Group records by deviceId
    final Map<String, List<Map<String, dynamic>>> grouped = {};
    for (final r in records) {
      final devId = (r['device_id'] ?? r['deviceId'] ?? 'BW-001-ALPHA').toString();
      grouped.putIfAbsent(devId, () => []).add(r);
    }

    bool hasChanged = false;

    grouped.forEach((deviceId, devRecords) {
      if (devRecords.isEmpty) return;
      final latest = devRecords.first;
      final temp = (latest['temperature'] as num?)?.toDouble() ?? 0.0;
      final hum = (latest['humidity'] as num?)?.toDouble() ?? 0.0;
      final batt = (latest['battery_level'] as num?)?.toInt() ?? 100;
      final rssi = (latest['wifi_rssi'] as num?)?.toInt() ?? -65;
      final audioPath = latest['audio_file_path'] as String?;

      int signalBars = 4;
      if (rssi >= -60) {
        signalBars = 4;
      } else if (rssi >= -70) {
        signalBars = 3;
      } else if (rssi >= -80) {
        signalBars = 2;
      } else {
        signalBars = 1;
      }

      // Extract real-time temperature, humidity, dates & acoustic history from SQLite records
      final tempHist = devRecords
          .map((r) => (r['temperature'] as num?)?.toDouble() ?? 0.0)
          .take(20)
          .toList()
          .reversed
          .toList();
      final humHist = devRecords
          .map((r) => (r['humidity'] as num?)?.toDouble() ?? 0.0)
          .take(20)
          .toList()
          .reversed
          .toList();
      final datesHist = devRecords
          .map((r) {
            final ts = (r['timestamp'] ?? r['created_at'] ?? '').toString();
            if (ts.contains('_')) {
              final parts = ts.split('_');
              if (parts.length > 1 && parts[1].length >= 4) {
                return '${parts[1].substring(0, 2)}:${parts[1].substring(2, 4)}';
              }
            } else if (ts.contains(':')) {
              final parts = ts.split(' ');
              return parts.length > 1 ? parts[1].substring(0, 5) : ts.substring(0, 5);
            }
            return ts.isNotEmpty ? ts : 'Now';
          })
          .take(20)
          .toList()
          .reversed
          .toList();
      final acousticHist = devRecords
          .map((r) {
            final f = ((r['frequency'] ?? r['frequency_hz'] ?? 0) as num).toDouble();
            if (f > 0) return (f / 5.0).clamp(20.0, 95.0);
            final peak = (r['peak_audio'] as num?)?.toDouble();
            if (peak != null && peak > 0) {
              return (peak / 50.0).clamp(20.0, 95.0);
            }
            return 0.0;
          })
          .take(20)
          .toList()
          .reversed
          .toList();

      // Extract acoustic frequency (Hz)
      final rawFreq = latest['frequency'] ?? latest['frequency_hz'];
      int freqHz = 0;
      if (rawFreq is num) {
        freqHz = rawFreq.toInt();
      } else if (rawFreq is String) {
        freqHz = int.tryParse(rawFreq.replaceAll(RegExp(r'[^0-9]'), '')) ?? 0;
      }
      final bool hasAcoustic = freqHz > 0;
      final String acousticStr = hasAcoustic ? '$freqHz Hz' : '0 Hz';
      final String acousticStatusStr = hasAcoustic ? 'Normal' : 'Not Detected (0 Hz)';

      // Find matching hive by deviceId, id, or name
      final index = _hives.indexWhere((h) =>
          h.deviceId.trim().toUpperCase() == deviceId.trim().toUpperCase() ||
          h.id.trim().toUpperCase() == deviceId.trim().toUpperCase() ||
          (h.name.trim().isNotEmpty && h.name.toUpperCase().contains(deviceId.toUpperCase())));

      if (index != -1) {
        final existing = _hives[index];
        _hives[index] = existing.copyWith(
          temperature: temp.toStringAsFixed(1),
          humidity: hum.toStringAsFixed(0),
          acoustic: acousticStr,
          acousticStatus: acousticStatusStr,
          batteryLevel: '$batt%',
          wifiStatus: 'Connected',
          signalBars: signalBars,
          updated: 'Just now',
          audioFilePath: audioPath ?? existing.audioFilePath,
          historyDates: datesHist.isNotEmpty ? datesHist : existing.historyDates,
          temperatureHistory: tempHist.isNotEmpty ? tempHist : existing.temperatureHistory,
          humidityHistory: humHist.isNotEmpty ? humHist : existing.humidityHistory,
          acousticHistory: acousticHist.isNotEmpty ? acousticHist : existing.acousticHistory,
        );
        hasChanged = true;
      }
    });

    if (hasChanged) {
      _saveToCache();
      ConnectivityService().recordSyncEvent();
      _debouncedNotify();
    }
  }

  @override
  void dispose() {
    _debounceTimer?.cancel();
    _hivesSubscription?.cancel();
    _authSubscription?.cancel();
    super.dispose();
  }
}
