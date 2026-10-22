-- Windows font rasterization, no generated Python or PowerShell source files.
local code=[====[
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName Microsoft.VisualBasic
$dir='C:/portable/OpenKoikatsu/output/street-social-city-production/activity-scenes-v1'
$plan=Get-Content ($dir+'/plan.json') -Raw -Encoding UTF8|ConvertFrom-Json
$rows=@()
foreach($property in $plan.signs.PSObject.Properties){
 $s=$property.Value
 $traditional=$s.translations.'zh-Hant'
 $simplified=[Microsoft.VisualBasic.Strings]::StrConv($traditional,[Microsoft.VisualBasic.VbStrConv]::SimplifiedChinese,2052)
 foreach($locale in @('zh-Hant','zh-Hans','en')){
  $value=if($locale -eq 'zh-Hans'){$simplified}else{$s.translations.$locale}
  $folder=$dir+'/signs/'+$locale
  [IO.Directory]::CreateDirectory($folder)|Out-Null
  $width=[int]$s.size[0];$height=[int]$s.size[1]
  $bmp=New-Object Drawing.Bitmap $width,$height
  $g=[Drawing.Graphics]::FromImage($bmp)
  $g.Clear([Drawing.Color]::FromArgb(24,68,66))
  if($s.id -eq 'bear'){$g.Clear([Drawing.Color]::FromArgb(124,65,78))}
  if($s.id -in @('reading','picturebooks','arts')){$g.Clear([Drawing.Color]::FromArgb(52,37,27))}
  if($s.cover -eq 'lamb'){
   $g.Clear([Drawing.Color]::FromArgb(207,234,244))
   $green=New-Object Drawing.SolidBrush ([Drawing.Color]::FromArgb(98,156,102));$white=New-Object Drawing.SolidBrush ([Drawing.Color]::Ivory);$dark=New-Object Drawing.SolidBrush ([Drawing.Color]::FromArgb(50,65,68))
   $g.FillEllipse($green,-120,420,760,400);$g.FillEllipse($white,125,280,260,185);$g.FillEllipse($white,295,275,105,105)
   $g.FillEllipse($dark,310,250,24,55);$g.FillEllipse($dark,374,278,22,45);$g.FillEllipse($dark,368,315,10,10)
   $g.FillRectangle($dark,160,440,20,62);$g.FillRectangle($dark,290,440,20,62)
   $green.Dispose();$white.Dispose();$dark.Dispose()
  }
  if($s.cover -eq 'fireworks'){
   $g.Clear([Drawing.Color]::FromArgb(20,35,68))
   foreach($burst in @(@(180,285,90),@(365,375,65),@(170,495,48))){
    $pen=New-Object Drawing.Pen ([Drawing.Color]::FromArgb(252,201,91)),5
    for($k=0;$k -lt 16;$k++){$a=$k*[Math]::PI/8;$g.DrawLine($pen,[single]($burst[0]+20*[Math]::Cos($a)),[single]($burst[1]+20*[Math]::Sin($a)),[single]($burst[0]+$burst[2]*[Math]::Cos($a)),[single]($burst[1]+$burst[2]*[Math]::Sin($a)))}
    $pen.Dispose()
   }
  }
  $g.TextRenderingHint=[Drawing.Text.TextRenderingHint]::AntiAliasGridFit
  $size=76
  do{$font=New-Object Drawing.Font 'Microsoft JhengHei',$size,([Drawing.FontStyle]::Bold),([Drawing.GraphicsUnit]::Pixel);$measure=$g.MeasureString($value,$font);if($measure.Width -gt ($width-84)){$font.Dispose();$size-=2}}while($measure.Width -gt ($width-84))
  $foreground=if($s.cover -eq 'lamb'){[Drawing.Color]::FromArgb(24,68,66)}else{[Drawing.Color]::FromArgb(245,236,208)}
  $brush=New-Object Drawing.SolidBrush $foreground
  $textY=if($s.cover){45}else{($height-$measure.Height)/2}
  $g.DrawString($value,$font,$brush,[single](($width-$measure.Width)/2),[single]$textY)
  $path=$folder+'/'+$s.id+'.png'
  $bmp.Save($path,[Drawing.Imaging.ImageFormat]::Png)
  $sha=[Security.Cryptography.SHA256]::Create();$digest=[BitConverter]::ToString($sha.ComputeHash([IO.File]::ReadAllBytes($path))).Replace('-','').ToLower();$sha.Dispose()
  $rows+=@{id=$s.id;locale=$locale;text=$value;path=$path;sha256=$digest}
  $brush.Dispose();$font.Dispose();$g.Dispose();$bmp.Dispose()
 }
}
@{status='passed';entries=$rows;locales=@('zh-Hant','zh-Hans','en')}|ConvertTo-Json -Depth 6|Set-Content ($dir+'/signs.json') -Encoding UTF8
]====]
assert(os.execute('powershell.exe -NoProfile -Command "'..code:gsub('\r?\n','; ')..'"'))
