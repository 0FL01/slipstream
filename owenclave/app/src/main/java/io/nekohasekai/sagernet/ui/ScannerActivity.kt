package io.nekohasekai.sagernet.ui

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Bundle
import android.view.MenuItem
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.camera.core.CameraSelector
import androidx.camera.core.ImageAnalysis
import androidx.camera.core.Preview
import androidx.camera.lifecycle.ProcessCameraProvider
import androidx.camera.view.PreviewView
import androidx.core.content.ContextCompat
import io.nekohasekai.sagernet.R
import io.nekohasekai.sagernet.databinding.LayoutScannerBinding
import io.nekohasekai.sagernet.fmt.dnstt.DnsttBean
import io.nekohasekai.sagernet.fmt.dnstt.parseDnsttProvisioning
import io.nekohasekai.sagernet.utils.ZxingQRCodeAnalyzer
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.Executors

class ScannerActivity : ThemedActivity() {

    companion object {
        const val EXTRA_TOKEN_ONLY = "tokenOnly"
        const val EXTRA_TOKEN = "token"
        const val EXTRA_DOMAIN = "domain"
        const val EXTRA_CURRENT_DOMAIN = "currentDomain"
    }

    private lateinit var binding: LayoutScannerBinding
    private lateinit var imageAnalysis: ImageAnalysis
    private val analysisExecutor = Executors.newSingleThreadExecutor()
    private val resultDelivered = AtomicBoolean(false)

    private val requestPermission =
        registerForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
            if (granted) startCamera() else finish()
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        if (!intent.getBooleanExtra(EXTRA_TOKEN_ONLY, false)) {
            finish()
            return
        }

        binding = LayoutScannerBinding.inflate(layoutInflater)
        setContentView(binding.root)
        setSupportActionBar(findViewById(R.id.toolbar))
        supportActionBar?.apply {
            setTitle(R.string.add_profile_methods_scan_qr_code)
            setDisplayHomeAsUpEnabled(true)
            setHomeAsUpIndicator(R.drawable.ic_navigation_close)
        }
        binding.previewView.implementationMode = PreviewView.ImplementationMode.COMPATIBLE

        if (ContextCompat.checkSelfPermission(this, Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED) {
            startCamera()
        } else {
            requestPermission.launch(Manifest.permission.CAMERA)
        }
    }

    private fun startCamera() {
        val future = ProcessCameraProvider.getInstance(this)
        future.addListener({
            try {
                val provider = future.get()
                val preview = Preview.Builder().build().also {
                    it.surfaceProvider = binding.previewView.surfaceProvider
                }
                imageAnalysis = ImageAnalysis.Builder().build().also { analysis ->
                    analysis.setAnalyzer(analysisExecutor, ZxingQRCodeAnalyzer({ value ->
                        val provisioning = runCatching {
                            parseDnsttProvisioning(
                                value,
                                intent.getStringExtra(EXTRA_CURRENT_DOMAIN) ?: DnsttBean.LEGACY_DOMAIN,
                            )
                        }.getOrNull()
                        if (provisioning != null && resultDelivered.compareAndSet(false, true)) {
                            runOnUiThread {
                                analysis.clearAnalyzer()
                                setResult(RESULT_OK, Intent()
                                    .putExtra(EXTRA_TOKEN, provisioning.token)
                                    .putExtra(EXTRA_DOMAIN, provisioning.domain))
                                finish()
                            }
                        }
                    }, ::showError))
                }
                provider.unbindAll()
                provider.bindToLifecycle(this, CameraSelector.DEFAULT_BACK_CAMERA, preview, imageAnalysis)
            } catch (error: Exception) {
                showError(error)
            }
        }, ContextCompat.getMainExecutor(this))
    }

    private fun showError(error: Exception?) {
        runOnUiThread {
            Toast.makeText(this, R.string.action_import_err, Toast.LENGTH_SHORT).show()
        }
    }

    override fun onOptionsItemSelected(item: MenuItem): Boolean {
        if (item.itemId == android.R.id.home) {
            finish()
            return true
        }
        return super.onOptionsItemSelected(item)
    }

    override fun onDestroy() {
        analysisExecutor.shutdownNow()
        super.onDestroy()
    }
}
