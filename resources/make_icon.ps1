# Genera resources/smac.ico: cuadrado negro con borde/marca cian.
# Formato ICO con imagenes PNG (valido en Windows Vista+).
Add-Type -AssemblyName System.Drawing

function Make-Png([int]$size) {
    $bmp = New-Object System.Drawing.Bitmap($size, $size)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.Clear([System.Drawing.Color]::Black)
    $penW = [math]::Max(1, [int]($size / 16))
    $pen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(255, 0, 229, 255), $penW)
    $g.DrawRectangle($pen, 0, 0, $size - 1, $size - 1)
    $brush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 0, 229, 255))
    $r = [int]($size * 0.18)
    $g.FillRectangle($brush, $r, $r, ($size - 2 * $r), ($size - 2 * $r))
    $g.Dispose()
    $out = New-Object System.IO.MemoryStream
    $bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    return , $out.ToArray()   # el operador coma evita el desenrollado del pipeline
}

$pngs = @((Make-Png 16), (Make-Png 32), (Make-Png 48))

$ms = New-Object System.IO.MemoryStream
$bw = New-Object System.IO.BinaryWriter($ms)

$bw.Write([uint16]0)            # reservado
$bw.Write([uint16]1)            # tipo: icono
$bw.Write([uint16]$pngs.Count)  # numero de imagenes

$offset = 6 + 16 * $pngs.Count
foreach ($png in $pngs) {
    $w = $png.Length
    $side = 16
    # Recuperamos el lado real leyendo el IHDR del PNG (bytes 16-19, big endian).
    $side = ($png[16] -shl 24) -bor ($png[17] -shl 16) -bor ($png[18] -shl 8) -bor $png[19]
    $bw.Write([byte]($side -band 0xFF))   # ancho (0 = 256)
    $bw.Write([byte]($side -band 0xFF))   # alto
    $bw.Write([byte]0)                    # paleta
    $bw.Write([byte]0)                    # reservado
    $bw.Write([uint16]1)                  # planos
    $bw.Write([uint16]32)                 # bits por pixel
    $bw.Write([uint32]$w)                 # tamano de los datos
    $bw.Write([uint32]$offset)            # offset
    $offset += $w
}

foreach ($png in $pngs) { $bw.Write($png) }
$bw.Flush()

[System.IO.File]::WriteAllBytes("$PSScriptRoot\smac.ico", $ms.ToArray())
Write-Host ("smac.ico generado: " + (Get-Item "$PSScriptRoot\smac.ico").Length + " bytes")
